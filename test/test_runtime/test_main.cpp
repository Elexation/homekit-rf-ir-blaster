#include <unity.h>

#include "boot_recovery.h"
#include "login_throttle.h"

using namespace runtime;

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
	return UNITY_END();
}
