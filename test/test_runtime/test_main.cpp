#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "auth_config.h"
#include "boot_recovery.h"
#include "client_key.h"
#include "crypto.h"
#include "csrf.h"
#include "encoding.h"
#include "login_throttle.h"
#include "password.h"
#include "ref_crypto.h"
#include "request_policy.h"
#include "security_headers.h"
#include "session.h"
#include "setup_state.h"

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
	Settings st;  // https on by default
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

// With HTTPS off, plain HTTP is served (opt-in).
static void test_policy_plain_http_allowed() {
	Settings st;
	st.https = false;
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

static std::vector<uint8_t> fromHex(const std::string& h) {
	std::vector<uint8_t> out;
	out.reserve(h.size() / 2);
	auto nib = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		return (c | 0x20) - 'a' + 10;  // lower-case the letter, then offset
	};
	for (size_t i = 0; i + 1 < h.size(); i += 2)
		out.push_back(static_cast<uint8_t>((nib(h[i]) << 4) | nib(h[i + 1])));
	return out;
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

// --- password hashing (auth core) ---

static const uint8_t kSalt16[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

// A derived record verifies its own password and rejects wrong ones (case-sensitive, empty).
static void test_password_derive_then_verify() {
	test::RefCrypto c;
	std::string rec = deriveCredential(c, "correct horse battery staple",
	                                   kSalt16, sizeof(kSalt16), 1000);
	TEST_ASSERT_TRUE(verifyPassword(c, "correct horse battery staple", rec));
	TEST_ASSERT_FALSE(verifyPassword(c, "Correct Horse Battery Staple", rec));
	TEST_ASSERT_FALSE(verifyPassword(c, "", rec));
}

// The record carries the algorithm tag, the iteration count, and exactly four fields.
static void test_password_record_format() {
	test::RefCrypto c;
	std::string rec = deriveCredential(c, "pw", kSalt16, sizeof(kSalt16), 4096);
	TEST_ASSERT_EQUAL_INT(0, static_cast<int>(rec.rfind("pbkdf2_sha256$4096$", 0)));
	int dollars = 0;
	for (char ch : rec)
		if (ch == '$') ++dollars;
	TEST_ASSERT_EQUAL_INT(3, dollars);
}

// Every shape of malformed record is rejected rather than crashing or matching.
static void test_password_rejects_malformed_record() {
	test::RefCrypto c;
	std::string dk32 = base64urlEncode(std::vector<uint8_t>(32, 0).data(), 32);
	TEST_ASSERT_FALSE(verifyPassword(c, "pw", ""));
	TEST_ASSERT_FALSE(verifyPassword(c, "pw", "pbkdf2_sha256$1000"));               // too few fields
	TEST_ASSERT_FALSE(verifyPassword(c, "pw", "scrypt$1000$c2FsdA$" + dk32));        // wrong algorithm
	TEST_ASSERT_FALSE(verifyPassword(c, "pw", "pbkdf2_sha256$0$c2FsdA$" + dk32));     // zero iterations
	TEST_ASSERT_FALSE(verifyPassword(c, "pw", "pbkdf2_sha256$abc$c2FsdA$" + dk32));   // non-digit iterations
	TEST_ASSERT_FALSE(verifyPassword(c, "pw", "pbkdf2_sha256$1000$****$" + dk32));    // bad base64url salt
	TEST_ASSERT_FALSE(verifyPassword(c, "pw", "pbkdf2_sha256$1000$$" + dk32));        // empty salt
	TEST_ASSERT_FALSE(verifyPassword(c, "pw", "pbkdf2_sha256$1000$c2FsdA$AAAA"));     // derived key wrong length
}

// Flipping a byte of the stored derived key makes the right password stop verifying.
static void test_password_rejects_tampered_record() {
	test::RefCrypto c;
	std::string rec = deriveCredential(c, "hunter2", kSalt16, sizeof(kSalt16), 2048);
	TEST_ASSERT_TRUE(verifyPassword(c, "hunter2", rec));

	std::string tampered = rec;
	char& first = tampered[rec.rfind('$') + 1];  // first char of the derived-key field
	first = (first == 'A') ? 'B' : 'A';
	TEST_ASSERT_FALSE(verifyPassword(c, "hunter2", tampered));
}

// The stored iteration count drives derivation: a record claiming the wrong count fails.
static void test_password_iteration_count_is_used() {
	test::RefCrypto c;
	std::vector<uint8_t> dkC1 =
		fromHex("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
	std::string salt = base64urlEncode(std::string("salt"));
	std::string good = "pbkdf2_sha256$1$" + salt + "$" + base64urlEncode(dkC1.data(), 32);
	std::string bad  = "pbkdf2_sha256$2$" + salt + "$" + base64urlEncode(dkC1.data(), 32);
	TEST_ASSERT_TRUE(verifyPassword(c, "password", good));
	TEST_ASSERT_FALSE(verifyPassword(c, "password", bad));
}

// A record hand-built from a published PBKDF2 vector verifies (parse + compare are correct).
static void test_password_known_vector_record() {
	test::RefCrypto c;
	// RFC 7914: P="password", S="salt", c=4096, dkLen=32.
	std::vector<uint8_t> dk =
		fromHex("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
	std::string rec =
		"pbkdf2_sha256$4096$" + base64urlEncode(std::string("salt")) + "$" +
		base64urlEncode(dk.data(), dk.size());
	TEST_ASSERT_TRUE(verifyPassword(c, "password", rec));
	TEST_ASSERT_FALSE(verifyPassword(c, "passwerd", rec));
}

// --- session tokens (auth core) ---

static const std::string kSecret = "0123456789abcdef0123456789abcdef";

static std::string sessionPayload(const std::string& tok) {
	std::vector<uint8_t> p;
	base64urlDecode(tok.substr(0, tok.find('.')), p);
	return std::string(p.begin(), p.end());
}

// A freshly minted token verifies, exposing the user and the user|iat binding.
static void test_session_mint_then_verify() {
	test::RefCrypto c;
	std::string tok = mintSession(c, kSecret, "admin", 1000);
	SessionResult r = verifySession(c, kSecret, tok, 1000);
	TEST_ASSERT_TRUE(r.valid);
	TEST_ASSERT_EQUAL_STRING("admin", r.user.c_str());
	TEST_ASSERT_EQUAL_STRING("admin|1000", r.binding.c_str());
	TEST_ASSERT_FALSE(r.expiredIdle);
	TEST_ASSERT_FALSE(r.expiredAbsolute);
}

// The idle window closes at exactly iat + idle timeout.
static void test_session_idle_expiry() {
	test::RefCrypto c;
	std::string tok = mintSession(c, kSecret, "admin", 0);
	TEST_ASSERT_TRUE(verifySession(c, kSecret, tok, kIdleTimeoutMs - 1).valid);
	SessionResult r = verifySession(c, kSecret, tok, kIdleTimeoutMs);
	TEST_ASSERT_FALSE(r.valid);
	TEST_ASSERT_TRUE(r.expiredIdle);
	TEST_ASSERT_FALSE(r.expiredAbsolute);
}

// The absolute cap rejects the token at iat + absolute timeout.
static void test_session_absolute_cap() {
	test::RefCrypto c;
	std::string tok = mintSession(c, kSecret, "admin", 0);
	SessionResult r = verifySession(c, kSecret, tok, kAbsoluteTimeoutMs);
	TEST_ASSERT_FALSE(r.valid);
	TEST_ASSERT_TRUE(r.expiredAbsolute);
}

// Refresh slides the idle deadline to now + timeout but never moves iat or the absolute cap.
static void test_session_refresh_slides_idle_not_absolute() {
	test::RefCrypto c;
	std::string tok0 = mintSession(c, kSecret, "admin", 0);
	TEST_ASSERT_EQUAL_STRING("admin|0|28800000|900000", sessionPayload(tok0).c_str());

	std::string tok1 = refreshSession(c, kSecret, tok0, 600000);  // refresh at 10 min
	TEST_ASSERT_FALSE(tok1.empty());
	TEST_ASSERT_EQUAL_STRING("admin|0|28800000|1500000", sessionPayload(tok1).c_str());

	// The slid token outlives the original's idle window.
	TEST_ASSERT_TRUE(verifySession(c, kSecret, tok1, 1200000).valid);
	TEST_ASSERT_TRUE(verifySession(c, kSecret, tok0, 1200000).expiredIdle);
}

// Refreshing a token whose idle window already closed returns nothing (no reviving the dead).
static void test_session_refresh_rejects_expired() {
	test::RefCrypto c;
	std::string tok = mintSession(c, kSecret, "admin", 0);
	TEST_ASSERT_TRUE(refreshSession(c, kSecret, tok, kIdleTimeoutMs).empty());
}

// Any edit to the payload or the signature breaks verification.
static void test_session_rejects_tampered_token() {
	test::RefCrypto c;
	std::string tok = mintSession(c, kSecret, "admin", 1000);

	std::string p = tok;
	p[0] = (p[0] == 'A') ? 'B' : 'A';  // a payload byte
	TEST_ASSERT_FALSE(verifySession(c, kSecret, p, 1000).valid);

	std::string s = tok;
	size_t dot = tok.find('.');
	s[dot + 1] = (s[dot + 1] == 'A') ? 'B' : 'A';  // a signature byte
	TEST_ASSERT_FALSE(verifySession(c, kSecret, s, 1000).valid);
}

// A token minted under a different secret never verifies.
static void test_session_wrong_secret() {
	test::RefCrypto c;
	std::string tok = mintSession(c, kSecret, "admin", 1000);
	TEST_ASSERT_FALSE(verifySession(c, "wrong-secret", tok, 1000).valid);
}

// Malformed tokens and invalid users are rejected, not parsed.
static void test_session_malformed_and_bad_user() {
	test::RefCrypto c;
	TEST_ASSERT_FALSE(verifySession(c, kSecret, "", 0).valid);
	TEST_ASSERT_FALSE(verifySession(c, kSecret, "no-separator", 0).valid);
	TEST_ASSERT_FALSE(verifySession(c, kSecret, "a.b.c", 0).valid);   // two separators
	TEST_ASSERT_TRUE(mintSession(c, kSecret, "ad|min", 1000).empty());  // '|' in user
	TEST_ASSERT_TRUE(mintSession(c, kSecret, "", 1000).empty());        // empty user
}

// --- CSRF (auth core) ---

static const uint8_t kCsrfRand[8] = {9, 8, 7, 6, 5, 4, 3, 2};

// A minted token verifies when the cookie and form copies match under the right binding.
static void test_csrf_mint_then_verify() {
	test::RefCrypto c;
	std::string tok = mintCsrf(c, kSecret, "admin|1000", kCsrfRand, sizeof(kCsrfRand));
	TEST_ASSERT_TRUE(verifyCsrf(c, kSecret, "admin|1000", tok, tok));
}

// Double-submit fails when the cookie and form tokens differ or the form copy is missing.
static void test_csrf_cookie_form_mismatch() {
	test::RefCrypto c;
	std::string tok = mintCsrf(c, kSecret, "admin|1000", kCsrfRand, sizeof(kCsrfRand));
	std::string other = mintCsrf(c, kSecret, "admin|1000", kCsrfRand, sizeof(kCsrfRand) - 1);
	TEST_ASSERT_FALSE(verifyCsrf(c, kSecret, "admin|1000", tok, other));
	TEST_ASSERT_FALSE(verifyCsrf(c, kSecret, "admin|1000", tok, ""));
}

// A token bound to one session is rejected against a different binding.
static void test_csrf_wrong_binding() {
	test::RefCrypto c;
	std::string tok = mintCsrf(c, kSecret, "admin|1000", kCsrfRand, sizeof(kCsrfRand));
	TEST_ASSERT_FALSE(verifyCsrf(c, kSecret, "admin|2000", tok, tok));
}

// A token signed under a different secret never verifies.
static void test_csrf_wrong_secret() {
	test::RefCrypto c;
	std::string tok = mintCsrf(c, kSecret, "admin|1000", kCsrfRand, sizeof(kCsrfRand));
	TEST_ASSERT_FALSE(verifyCsrf(c, "wrong-secret", "admin|1000", tok, tok));
}

// Tampering with the matched token breaks the signature; malformed tokens are rejected.
static void test_csrf_tampered_and_malformed() {
	test::RefCrypto c;
	std::string tok = mintCsrf(c, kSecret, "admin|1000", kCsrfRand, sizeof(kCsrfRand));
	std::string bad = tok;
	bad[0] = (bad[0] == 'A') ? 'B' : 'A';  // first rand char; avoids base64 trailing-bit no-ops
	TEST_ASSERT_FALSE(verifyCsrf(c, kSecret, "admin|1000", bad, bad));
	TEST_ASSERT_FALSE(verifyCsrf(c, kSecret, "admin|1000", "no-dot", "no-dot"));
	TEST_ASSERT_FALSE(verifyCsrf(c, kSecret, "admin|1000", "a.b.c", "a.b.c"));
}

// --- security headers (auth core) ---

// The session cookie carries the __Host- prefix and the full hardening attribute set.
static void test_session_cookie_format() {
	TEST_ASSERT_EQUAL_STRING(
		"__Host-SID=tok123; Secure; HttpOnly; SameSite=Strict; Path=/",
		sessionCookie("tok123").c_str());
	TEST_ASSERT_EQUAL_STRING(
		"__Host-SID=; Secure; HttpOnly; SameSite=Strict; Path=/; Max-Age=0",
		clearSessionCookie().c_str());
}

// The CSRF cookie mirrors those attributes under its own name.
static void test_csrf_cookie_format() {
	TEST_ASSERT_EQUAL_STRING(
		"__Host-CSRF=abc.def; Secure; HttpOnly; SameSite=Strict; Path=/",
		csrfCookie("abc.def").c_str());
}

// HSTS pins one year, no includeSubDomains or preload.
static void test_hsts_header() {
	TEST_ASSERT_EQUAL_STRING("max-age=31536000", hstsHeader().c_str());
}

// The static hardening headers carry their exact names and values.
static void test_static_security_headers() {
	TEST_ASSERT_EQUAL_UINT(5, kStaticSecurityHeaderCount);
	TEST_ASSERT_EQUAL_STRING("X-Content-Type-Options", kStaticSecurityHeaders[0].name);
	TEST_ASSERT_EQUAL_STRING("nosniff", kStaticSecurityHeaders[0].value);
	TEST_ASSERT_EQUAL_STRING("X-Frame-Options", kStaticSecurityHeaders[1].name);
	TEST_ASSERT_EQUAL_STRING("DENY", kStaticSecurityHeaders[1].value);
	TEST_ASSERT_EQUAL_STRING("Referrer-Policy", kStaticSecurityHeaders[2].name);
	TEST_ASSERT_EQUAL_STRING("no-referrer", kStaticSecurityHeaders[2].value);
	TEST_ASSERT_EQUAL_STRING("Cache-Control", kStaticSecurityHeaders[3].name);
	TEST_ASSERT_EQUAL_STRING("no-store", kStaticSecurityHeaders[3].value);
	TEST_ASSERT_EQUAL_STRING("Content-Security-Policy", kStaticSecurityHeaders[4].name);
	TEST_ASSERT_EQUAL_STRING(
		"default-src 'self'; frame-ancestors 'none'; form-action 'self'; "
		"base-uri 'none'; object-src 'none'",
		kStaticSecurityHeaders[4].value);
}

// --- first-boot setup (auth core) ---

// Before any credential the device awaits setup; once one exists it is configured.
static void test_setup_current_mode() {
	TEST_ASSERT_EQUAL_INT(static_cast<int>(SetupMode::AwaitingSetup),
	                      static_cast<int>(currentMode(false)));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(SetupMode::Configured),
	                      static_cast<int>(currentMode(true)));
}

// Once configured, setup is refused even with a matching nonce or an open window.
static void test_setup_configured_rejects() {
	TEST_ASSERT_FALSE(setupAllowed(true, "nonce", "nonce", 10000, 0));
}

// A matching stored nonce opens setup (the bench second factor).
static void test_setup_valid_nonce_allows() {
	TEST_ASSERT_TRUE(setupAllowed(false, "abc123", "abc123", 0, 1000));
}

// An open button window opens setup without a nonce; it closes at the deadline.
static void test_setup_window_open_allows() {
	TEST_ASSERT_TRUE(setupAllowed(false, "", "", 5000, 4999));
	TEST_ASSERT_FALSE(setupAllowed(false, "", "", 5000, 5000));
}

// With neither a valid nonce nor an open window, setup is refused.
static void test_setup_neither_rejects() {
	TEST_ASSERT_FALSE(setupAllowed(false, "", "", 0, 1000));
}

// A wrong nonce, or an empty stored nonce, never grants setup on its own.
static void test_setup_wrong_nonce_rejects() {
	TEST_ASSERT_FALSE(setupAllowed(false, "wrong", "right", 0, 1000));
	TEST_ASSERT_FALSE(setupAllowed(false, "", "", 0, 1000));
	TEST_ASSERT_FALSE(setupAllowed(false, "anything", "", 0, 1000));
}

// makeNonce base64url-encodes the supplied random bytes (exercises '-').
static void test_setup_make_nonce() {
	const uint8_t r[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
	TEST_ASSERT_EQUAL_STRING("3q2-7wEC", makeNonce(r, sizeof(r)).c_str());
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
	RUN_TEST(test_policy_plain_http_allowed);
	RUN_TEST(test_policy_trusted_forwarded_proto_https_serves);
	RUN_TEST(test_policy_untrusted_forwarded_proto_ignored);
	RUN_TEST(test_sha256_kat);
	RUN_TEST(test_hmac_sha256_kat);
	RUN_TEST(test_pbkdf2_kat);
	RUN_TEST(test_base64url_known_and_roundtrip);
	RUN_TEST(test_base64url_rejects_invalid);
	RUN_TEST(test_constant_time_equal);
	RUN_TEST(test_password_derive_then_verify);
	RUN_TEST(test_password_record_format);
	RUN_TEST(test_password_rejects_malformed_record);
	RUN_TEST(test_password_rejects_tampered_record);
	RUN_TEST(test_password_iteration_count_is_used);
	RUN_TEST(test_password_known_vector_record);
	RUN_TEST(test_session_mint_then_verify);
	RUN_TEST(test_session_idle_expiry);
	RUN_TEST(test_session_absolute_cap);
	RUN_TEST(test_session_refresh_slides_idle_not_absolute);
	RUN_TEST(test_session_refresh_rejects_expired);
	RUN_TEST(test_session_rejects_tampered_token);
	RUN_TEST(test_session_wrong_secret);
	RUN_TEST(test_session_malformed_and_bad_user);
	RUN_TEST(test_csrf_mint_then_verify);
	RUN_TEST(test_csrf_cookie_form_mismatch);
	RUN_TEST(test_csrf_wrong_binding);
	RUN_TEST(test_csrf_wrong_secret);
	RUN_TEST(test_csrf_tampered_and_malformed);
	RUN_TEST(test_session_cookie_format);
	RUN_TEST(test_csrf_cookie_format);
	RUN_TEST(test_hsts_header);
	RUN_TEST(test_static_security_headers);
	RUN_TEST(test_setup_current_mode);
	RUN_TEST(test_setup_configured_rejects);
	RUN_TEST(test_setup_valid_nonce_allows);
	RUN_TEST(test_setup_window_open_allows);
	RUN_TEST(test_setup_neither_rejects);
	RUN_TEST(test_setup_wrong_nonce_rejects);
	RUN_TEST(test_setup_make_nonce);
	return UNITY_END();
}
