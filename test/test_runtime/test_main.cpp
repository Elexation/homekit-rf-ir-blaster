#include <unity.h>

#include <string>

#include "boot_recovery.h"
#include "client_key.h"
#include "login_throttle.h"
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
	return UNITY_END();
}
