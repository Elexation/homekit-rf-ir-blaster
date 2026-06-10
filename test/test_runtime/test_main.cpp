#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "boot_recovery.h"
#include "client_key.h"
#include "crypto.h"
#include "encoding.h"
#include "login_throttle.h"
#include "ref_crypto.h"
#include "request_policy.h"

using namespace runtime;
using config::Settings;

void setUp() {}
void tearDown() {}

// --- LoginThrottle ---

// The Nth (5th) failure locks; the table starts unlocked.
static void test_locks_at_threshold() {
	LoginThrottle t;
	for (int i = 0; i < LoginThrottle::kMaxFailures; ++i) {
		TEST_ASSERT_FALSE(t.isLocked(1, 100));
		t.recordFailure(1, 100);
	}
	TEST_ASSERT_TRUE(t.isLocked(1, 100));
}

// One below the threshold stays unlocked.
static void test_below_threshold_not_locked() {
	LoginThrottle t;
	for (int i = 0; i < LoginThrottle::kMaxFailures - 1; ++i)
		t.recordFailure(7, 100);
	TEST_ASSERT_FALSE(t.isLocked(7, 100));
}

// A lockout expires after the window, and the counter resets (no instant re-lock).
static void test_lockout_expires_and_resets() {
	LoginThrottle t;
	for (int i = 0; i < LoginThrottle::kMaxFailures; ++i)
		t.recordFailure(3, 1000);
	TEST_ASSERT_TRUE(t.isLocked(3, 1000));

	uint32_t after = 1000 + LoginThrottle::kLockoutMs;
	TEST_ASSERT_FALSE(t.isLocked(3, after));
	t.recordFailure(3, after);                 // a single fresh failure...
	TEST_ASSERT_FALSE(t.isLocked(3, after));   // ...must not immediately re-lock
}

// A success clears accumulated failures below the threshold.
static void test_success_resets_failures() {
	LoginThrottle t;
	for (int i = 0; i < LoginThrottle::kMaxFailures - 1; ++i)
		t.recordFailure(9, 50);
	t.recordSuccess(9, 60);
	for (int i = 0; i < LoginThrottle::kMaxFailures - 1; ++i)
		t.recordFailure(9, 70);
	TEST_ASSERT_FALSE(t.isLocked(9, 70));  // counter restarted; 4 < 5
}

// A success during an active lockout does not unlock (lockout wins).
static void test_success_during_lockout_stays_locked() {
	LoginThrottle t;
	for (int i = 0; i < LoginThrottle::kMaxFailures; ++i)
		t.recordFailure(2, 200);
	TEST_ASSERT_TRUE(t.isLocked(2, 200));
	t.recordSuccess(2, 250);
	TEST_ASSERT_TRUE(t.isLocked(2, 250));
}

// Failures on one key never lock a different key.
static void test_independent_keys() {
	LoginThrottle t;
	for (int i = 0; i < LoginThrottle::kMaxFailures; ++i)
		t.recordFailure(100, 10);
	TEST_ASSERT_TRUE(t.isLocked(100, 10));
	TEST_ASSERT_FALSE(t.isLocked(200, 10));
}

// When the table is full, eviction spares a locked entry and drops an unlocked one,
// even though the locked entry is the least-recently-seen.
static void test_eviction_prefers_unlocked() {
	LoginThrottle t;
	for (int i = 0; i < LoginThrottle::kMaxFailures; ++i)
		t.recordFailure(0, 1);             // key 0 locked, oldest lastSeen
	TEST_ASSERT_TRUE(t.isLocked(0, 1));

	for (uint32_t k = 1; k < LoginThrottle::kMaxTrackedClients; ++k)
		t.recordFailure(k, 10 + k);        // fill the rest, one failure each (unlocked)

	t.recordFailure(9999, 1000);           // forces an eviction
	TEST_ASSERT_TRUE(t.isLocked(0, 1000)); // locked victim survived
	TEST_ASSERT_FALSE(t.isLocked(9999, 1000));
}

// A backward clock never unlocks early and never reports negative time remaining.
static void test_backward_clock_no_early_unlock() {
	LoginThrottle t;
	for (int i = 0; i < LoginThrottle::kMaxFailures; ++i)
		t.recordFailure(5, 100000);
	TEST_ASSERT_TRUE(t.isLocked(5, 100000));

	TEST_ASSERT_TRUE(t.isLocked(5, 50000));  // clock jumped back
	TEST_ASSERT_TRUE(t.timeRemainingMs(5, 50000) >= LoginThrottle::kLockoutMs);

	t.recordFailure(5, 50000);  // must not shorten the lockout
	TEST_ASSERT_TRUE(t.isLocked(5, 100000 + LoginThrottle::kLockoutMs - 1));
}

// --- boot_recovery ---

// First boot: count becomes 1, no safe mode, clear time is now + stable window.
static void test_first_boot() {
	BootDecision d = evaluateBoot(0, 5000);
	TEST_ASSERT_EQUAL_UINT8(1, d.newCount);
	TEST_ASSERT_FALSE(d.enterSafeMode);
	TEST_ASSERT_EQUAL_UINT32(5000 + kStableRunMs, d.clearAtMs);
}

// The third consecutive boot trips safe mode.
static void test_third_boot_triggers_safe_mode() {
	BootDecision d = evaluateBoot(2, 0);
	TEST_ASSERT_EQUAL_UINT8(3, d.newCount);
	TEST_ASSERT_TRUE(d.enterSafeMode);
}

// The count saturates at the cap; garbage above the cap saturates too, never wraps.
static void test_boot_count_saturates() {
	BootDecision d = evaluateBoot(250, 0);
	TEST_ASSERT_EQUAL_UINT8(250, d.newCount);
	TEST_ASSERT_TRUE(d.enterSafeMode);
	BootDecision g = evaluateBoot(255, 0);
	TEST_ASSERT_EQUAL_UINT8(250, g.newCount);
}

// The clear predicate fires at and after the clear time, not before.
static void test_should_clear_counter_boundaries() {
	TEST_ASSERT_FALSE(shouldClearCounter(9999, 10000));
	TEST_ASSERT_TRUE(shouldClearCounter(10000, 10000));
	TEST_ASSERT_TRUE(shouldClearCounter(10001, 10000));
}

// Chaining three cycles (each feeding the prior newCount) reaches safe mode.
static void test_three_cycle_chain() {
	BootDecision b1 = evaluateBoot(0, 0);
	TEST_ASSERT_FALSE(b1.enterSafeMode);
	BootDecision b2 = evaluateBoot(b1.newCount, 0);
	TEST_ASSERT_FALSE(b2.enterSafeMode);
	BootDecision b3 = evaluateBoot(b2.newCount, 0);
	TEST_ASSERT_TRUE(b3.enterSafeMode);
	TEST_ASSERT_EQUAL_UINT8(3, b3.newCount);
}

// --- client_key ---

static constexpr uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
	return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | d;
}

// Without a trusted proxy the socket peer is the key; forwarded headers are ignored.
static void test_client_key_direct_uses_peer() {
	uint32_t peer = ip(192, 168, 1, 50);
	TEST_ASSERT_EQUAL_UINT32(peer, deriveClientKey(peer, false, "10.0.0.9", "10.0.0.9"));
}

// A trusted proxy's X-Real-IP becomes the key.
static void test_client_key_trusted_x_real_ip() {
	uint32_t peer = ip(10, 0, 0, 1);
	TEST_ASSERT_EQUAL_UINT32(ip(203, 0, 113, 7),
	                         deriveClientKey(peer, true, "203.0.113.7", ""));
}

// With no X-Real-IP, the first X-Forwarded-For hop (the original client) is the key.
static void test_client_key_trusted_forwarded_for_first_hop() {
	uint32_t peer = ip(10, 0, 0, 1);
	TEST_ASSERT_EQUAL_UINT32(ip(198, 51, 100, 4),
	                         deriveClientKey(peer, true, "", "198.51.100.4, 10.0.0.1"));
}

// A trusted proxy with a missing or malformed header falls back to the socket peer.
static void test_client_key_trusted_bad_header_falls_back() {
	uint32_t peer = ip(10, 0, 0, 1);
	TEST_ASSERT_EQUAL_UINT32(peer, deriveClientKey(peer, true, "not-an-ip", ""));
	TEST_ASSERT_EQUAL_UINT32(peer, deriveClientKey(peer, true, "", ""));
}

// Forwarded headers from an untrusted client cannot move the key (anti-spoof).
static void test_client_key_untrusted_ignores_spoofed_headers() {
	uint32_t peer = ip(192, 168, 1, 77);
	TEST_ASSERT_EQUAL_UINT32(peer, deriveClientKey(peer, false, "1.2.3.4", "1.2.3.4"));
}

// --- request_policy ---

static Request req(Scheme s, const std::string& host, const std::string& target,
                   bool loopback = false, const std::string& xfproto = "") {
	Request r;
	r.transportScheme = s;
	r.host = host;
	r.target = target;
	r.isLoopback = loopback;
	r.forwardedProto = xfproto;
	return r;
}

// Loopback bypasses every redirect and is always served.
static void test_policy_loopback_always_serves() {
	Settings st;  // https + redirect + require all on by default
	Decision d = evaluate(req(Scheme::Http, "127.0.0.1", "/", true), st);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::Serve), static_cast<int>(d.action));
}

// A plaintext request is upgraded to HTTPS on the canonical host, target preserved.
static void test_policy_http_upgrades_to_https() {
	Settings st;
	st.canonicalDomain = "blaster.local";
	Decision d = evaluate(req(Scheme::Http, "192.168.1.50", "/settings?x=1"), st);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::Redirect), static_cast<int>(d.action));
	TEST_ASSERT_EQUAL_STRING("https://blaster.local/settings?x=1", d.location.c_str());
}

// Over HTTPS, a mismatched Host is 301'd to the canonical host.
static void test_policy_canonical_host_redirect() {
	Settings st;
	st.canonicalDomain = "blaster.local";
	Decision d = evaluate(req(Scheme::Https, "192.168.1.50", "/"), st);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::Redirect), static_cast<int>(d.action));
	TEST_ASSERT_EQUAL_STRING("https://blaster.local/", d.location.c_str());
}

// The canonical host over HTTPS is served; a port suffix, case, and trailing dot match.
static void test_policy_canonical_host_served() {
	Settings st;
	st.canonicalDomain = "blaster.local";
	TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::Serve),
	    static_cast<int>(evaluate(req(Scheme::Https, "blaster.local", "/"), st).action));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::Serve),
	    static_cast<int>(evaluate(req(Scheme::Https, "blaster.local:443", "/"), st).action));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::Serve),
	    static_cast<int>(evaluate(req(Scheme::Https, "BLASTER.local.", "/"), st).action));
}

// require-HTTPS without the redirect refuses plaintext outright.
static void test_policy_require_https_rejects_plaintext() {
	Settings st;
	st.httpToHttpsRedirect = false;
	st.requireHttps = true;
	Decision d = evaluate(req(Scheme::Http, "blaster.local", "/"), st);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::Reject), static_cast<int>(d.action));
}

// With HTTPS off and neither redirect nor require, plain HTTP is served (opt-in).
static void test_policy_plain_http_allowed() {
	Settings st;
	st.https = false;
	st.httpToHttpsRedirect = false;
	st.requireHttps = false;
	st.trustedProxy = false;
	Decision d = evaluate(req(Scheme::Http, "192.168.1.50", "/"), st);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::Serve), static_cast<int>(d.action));
}

// A trusted proxy's X-Forwarded-Proto=https is honored: no redirect loop.
static void test_policy_trusted_forwarded_proto_https_serves() {
	Settings st;
	st.trustedProxy = true;
	st.canonicalDomain = "blaster.local";
	Decision d = evaluate(req(Scheme::Http, "blaster.local", "/", false, "https"), st);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::Serve), static_cast<int>(d.action));
}

// X-Forwarded-Proto is ignored without a trusted proxy (anti-spoof): still upgraded.
static void test_policy_untrusted_forwarded_proto_ignored() {
	Settings st;
	st.trustedProxy = false;
	st.canonicalDomain = "blaster.local";
	Decision d = evaluate(req(Scheme::Http, "blaster.local", "/", false, "https"), st);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::Redirect), static_cast<int>(d.action));
	TEST_ASSERT_EQUAL_STRING("https://blaster.local/", d.location.c_str());
}

// --- crypto / encoding (auth core) ---

static std::string toHex(const uint8_t* p, size_t n) {
	static const char* h = "0123456789abcdef";
	std::string s;
	s.reserve(n * 2);
	for (size_t i = 0; i < n; ++i) {
		s.push_back(h[p[i] >> 4]);
		s.push_back(h[p[i] & 0xF]);
	}
	return s;
}

static const uint8_t* bytes(const std::string& s) {
	return reinterpret_cast<const uint8_t*>(s.data());
}

// SHA-256 known-answer vectors (FIPS 180-4).
static void test_sha256_kat() {
	test::RefCrypto c;
	uint8_t out[32];
	c.sha256(bytes("abc"), 3, out);
	TEST_ASSERT_EQUAL_STRING(
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
		toHex(out, 32).c_str());
	c.sha256(bytes(""), 0, out);
	TEST_ASSERT_EQUAL_STRING(
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
		toHex(out, 32).c_str());
}

// HMAC-SHA256 known-answer vectors (RFC 4231 cases 1 and 2).
static void test_hmac_sha256_kat() {
	test::RefCrypto c;
	uint8_t out[32];
	uint8_t key1[20];
	memset(key1, 0x0b, 20);
	std::string d1 = "Hi There";
	c.hmacSha256(key1, 20, bytes(d1), d1.size(), out);
	TEST_ASSERT_EQUAL_STRING(
		"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
		toHex(out, 32).c_str());

	std::string k2 = "Jefe", d2 = "what do ya want for nothing?";
	c.hmacSha256(bytes(k2), k2.size(), bytes(d2), d2.size(), out);
	TEST_ASSERT_EQUAL_STRING(
		"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
		toHex(out, 32).c_str());
}

static std::string pbkdf2Hex(const test::RefCrypto& c, uint32_t iters) {
	std::string p = "password", s = "salt";
	uint8_t out[32];
	c.pbkdf2HmacSha256(bytes(p), p.size(), bytes(s), s.size(), iters, out, 32);
	return toHex(out, 32);
}

// PBKDF2-HMAC-SHA256 known-answer vectors (RFC 7914 appendix; password/salt, dkLen 32).
static void test_pbkdf2_kat() {
	test::RefCrypto c;
	TEST_ASSERT_EQUAL_STRING(
		"120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b",
		pbkdf2Hex(c, 1).c_str());
	TEST_ASSERT_EQUAL_STRING(
		"ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43",
		pbkdf2Hex(c, 2).c_str());
	TEST_ASSERT_EQUAL_STRING(
		"c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a",
		pbkdf2Hex(c, 4096).c_str());
}

// Known base64url strings plus a full 0..255 round-trip that exercises '-' and '_'.
static void test_base64url_known_and_roundtrip() {
	TEST_ASSERT_EQUAL_STRING("Zm9vYmFy", base64urlEncode(std::string("foobar")).c_str());
	TEST_ASSERT_EQUAL_STRING("Zg", base64urlEncode(std::string("f")).c_str());
	TEST_ASSERT_EQUAL_STRING("", base64urlEncode(std::string("")).c_str());

	std::vector<uint8_t> all(256);
	for (int i = 0; i < 256; ++i) all[i] = uint8_t(i);
	std::string enc = base64urlEncode(all.data(), all.size());
	TEST_ASSERT_TRUE(enc.find('+') == std::string::npos);
	TEST_ASSERT_TRUE(enc.find('/') == std::string::npos);
	TEST_ASSERT_TRUE(enc.find('=') == std::string::npos);

	std::vector<uint8_t> back;
	TEST_ASSERT_TRUE(base64urlDecode(enc, back));
	TEST_ASSERT_EQUAL_UINT(256, back.size());
	TEST_ASSERT_EQUAL_INT(0, memcmp(all.data(), back.data(), 256));
}

static void test_base64url_rejects_invalid() {
	std::vector<uint8_t> out;
	TEST_ASSERT_FALSE(base64urlDecode("####", out));  // non-alphabet
	TEST_ASSERT_FALSE(base64urlDecode("Zg=", out));    // '=' not in the url alphabet
	TEST_ASSERT_FALSE(base64urlDecode("A", out));      // impossible length (1 mod 4)
}

static void test_constant_time_equal() {
	uint8_t a[4] = {1, 2, 3, 4};
	uint8_t b[4] = {1, 2, 3, 4};
	uint8_t d[4] = {1, 2, 3, 5};
	TEST_ASSERT_TRUE(constantTimeEqual(a, b, 4));
	TEST_ASSERT_FALSE(constantTimeEqual(a, d, 4));
	TEST_ASSERT_TRUE(constantTimeEqual(std::string("abc"), std::string("abc")));
	TEST_ASSERT_FALSE(constantTimeEqual(std::string("abc"), std::string("abcd")));
	TEST_ASSERT_FALSE(constantTimeEqual(std::string("abc"), std::string("abd")));
}

int main(int, char**) {
	UNITY_BEGIN();
	RUN_TEST(test_locks_at_threshold);
	RUN_TEST(test_below_threshold_not_locked);
	RUN_TEST(test_lockout_expires_and_resets);
	RUN_TEST(test_success_resets_failures);
	RUN_TEST(test_success_during_lockout_stays_locked);
	RUN_TEST(test_independent_keys);
	RUN_TEST(test_eviction_prefers_unlocked);
	RUN_TEST(test_backward_clock_no_early_unlock);
	RUN_TEST(test_first_boot);
	RUN_TEST(test_third_boot_triggers_safe_mode);
	RUN_TEST(test_boot_count_saturates);
	RUN_TEST(test_should_clear_counter_boundaries);
	RUN_TEST(test_three_cycle_chain);
	RUN_TEST(test_client_key_direct_uses_peer);
	RUN_TEST(test_client_key_trusted_x_real_ip);
	RUN_TEST(test_client_key_trusted_forwarded_for_first_hop);
	RUN_TEST(test_client_key_trusted_bad_header_falls_back);
	RUN_TEST(test_client_key_untrusted_ignores_spoofed_headers);
	RUN_TEST(test_policy_loopback_always_serves);
	RUN_TEST(test_policy_http_upgrades_to_https);
	RUN_TEST(test_policy_canonical_host_redirect);
	RUN_TEST(test_policy_canonical_host_served);
	RUN_TEST(test_policy_require_https_rejects_plaintext);
	RUN_TEST(test_policy_plain_http_allowed);
	RUN_TEST(test_policy_trusted_forwarded_proto_https_serves);
	RUN_TEST(test_policy_untrusted_forwarded_proto_ignored);
	RUN_TEST(test_sha256_kat);
	RUN_TEST(test_hmac_sha256_kat);
	RUN_TEST(test_pbkdf2_kat);
	RUN_TEST(test_base64url_known_and_roundtrip);
	RUN_TEST(test_base64url_rejects_invalid);
	RUN_TEST(test_constant_time_equal);
	return UNITY_END();
}
