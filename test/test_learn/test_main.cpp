#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "config_codec.h"
#include "config_model.h"
#include "config_mutate.h"
#include "config_validate.h"
#include "frame_merge.h"
#include "learn_machine.h"
#include "memory_blob_store.h"
#include "signal_match.h"

using namespace config;
using namespace learn;

void setUp() {}
void tearDown() {}

// NEC-like IR frame: lead pair, 16 bit pairs, final mark; 35 entries, odd.
static std::vector<uint16_t> irFrame() {
	std::vector<uint16_t> p{ 9000, 4500 };
	for (int i = 0; i < 16; ++i) {
		p.push_back(560);
		p.push_back(i % 3 == 0 ? 1690 : 560);
	}
	p.push_back(560);
	return p;
}

// Fixed-code-like RF frame: 24 bit pairs from `bits`, final mark; 49 entries. Same
// lengths and timing alphabet for any bits, so distinct payloads stay structurally alike.
static std::vector<uint16_t> rfFrame(uint32_t bits) {
	std::vector<uint16_t> p;
	for (int i = 0; i < 24; ++i) {
		bool one = (bits >> i) & 1u;
		p.push_back(one ? 1050 : 350);
		p.push_back(one ? 350 : 1050);
	}
	p.push_back(350);
	return p;
}

static std::vector<uint16_t> jitter(std::vector<uint16_t> p, int delta) {
	for (auto& v : p)
		v = static_cast<uint16_t>(v + delta);
	return p;
}

// PT2262-style frame: each bit a (mark, space) pair (1 = long/short, 0 = short/long)
// plus a trailing sync mark, so it ends on a mark (odd), like a collapsed OOK burst.
static std::vector<uint16_t> pt2262Frame(const std::vector<int>& bits) {
	const uint16_t A = 416, L = 1248;
	std::vector<uint16_t> f;
	for (int b : bits) {
		f.push_back(b ? L : A);
		f.push_back(b ? A : L);
	}
	f.push_back(A);
	return f;
}

// Simulate an AGC clip of one bit: its long mark reads short and the following
// space reads long (only ever shortens a mark), flipping a 1 toward a 0.
static std::vector<uint16_t> clipBit(std::vector<uint16_t> f, size_t bit) {
	f[2 * bit] = 416;
	f[2 * bit + 1] = 1248;
	return f;
}

// Concatenate frames with the reset gap between them; like a captured burst,
// the final trailing gap is dropped so it ends on a mark.
static std::vector<uint16_t> burstOf(const std::vector<std::vector<uint16_t>>& frames,
                                     uint16_t gap) {
	std::vector<uint16_t> out;
	for (size_t i = 0; i < frames.size(); ++i) {
		out.insert(out.end(), frames[i].begin(), frames[i].end());
		if (i + 1 < frames.size())
			out.push_back(gap);
	}
	return out;
}

// A one-off ambient burst of a given length (no reset gap); varying lengths stay
// structurally distinct so they aren't read as rolling.
static std::vector<uint16_t> noiseFrame(size_t len) {
	std::vector<uint16_t> f;
	for (size_t i = 0; i < len; ++i)
		f.push_back(static_cast<uint16_t>(300 + (i % 5) * 100));
	return f;
}

static uint64_t durMs(const std::vector<uint16_t>& p) {
	uint64_t total = 0;
	for (auto v : p)
		total += v;
	return total / 1000;
}

// Feeds a burst that starts gapMs after prevEnd; returns the burst's end time.
static uint64_t feedAfter(LearnMachine& m, Source s, const std::vector<uint16_t>& p,
                          uint16_t carrier, uint64_t prevEnd, uint32_t gapMs) {
	uint64_t end = prevEnd + gapMs + durMs(p);
	m.feedBurst(s, p.data(), p.size(), carrier, end);
	return end;
}

static StoredCode makeRf(uint16_t freq, std::vector<uint16_t> pulses) {
	StoredCode c;
	c.kind = CodeKind::RF;
	c.freqMHz = freq;
	c.pulses = std::move(pulses);
	return c;
}

static const uint64_t T0 = 1000;

// --- signal matching ---

// 25 percent of the larger value or 100 microseconds, whichever is wider.
static void test_duration_tolerance() {
	TEST_ASSERT_TRUE(durationsMatch(1000, 1250));   // exactly 25 percent
	TEST_ASSERT_FALSE(durationsMatch(1000, 1400));  // past both bounds
	TEST_ASSERT_TRUE(durationsMatch(50, 140));      // 90 us under the absolute floor
	TEST_ASSERT_TRUE(durationsMatch(560, 560));
}

// --- frame de-clip (multi-frame merge) ---

// A burst with no reset gap is one frame: passed through unchanged, no gap.
static void test_declip_single_frame_passthrough() {
	std::vector<uint16_t> f = pt2262Frame({ 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1 });
	uint16_t out[256];
	uint16_t gap = 99;
	size_t n = declipBurst(f.data(), f.size(), out, 256, &gap);
	TEST_ASSERT_EQUAL_UINT32(f.size(), n);
	TEST_ASSERT_EQUAL_UINT16_ARRAY(f.data(), out, n);
	TEST_ASSERT_EQUAL_UINT16(0, gap);
}

// A bit clipped in two of three frames is recovered from the frame that read
// it long: longest mark / shortest space per slot wins.
static void test_declip_recovers_clipped_float() {
	std::vector<uint16_t> good = pt2262Frame({ 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1 });
	std::vector<uint16_t> bad = clipBit(good, 0);
	std::vector<uint16_t> burst = burstOf({ bad, good, bad }, 12896);
	uint16_t out[256];
	uint16_t gap = 0;
	size_t n = declipBurst(burst.data(), burst.size(), out, 256, &gap);
	TEST_ASSERT_EQUAL_UINT32(good.size(), n);
	TEST_ASSERT_EQUAL_UINT16_ARRAY(good.data(), out, n);
	TEST_ASSERT_EQUAL_UINT16(12896, gap);
}

// A partial leading or truncated trailing frame is off the modal length and
// never pollutes the merge.
static void test_declip_drops_ragged_frames() {
	std::vector<uint16_t> good = pt2262Frame({ 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1 });
	std::vector<uint16_t> bad = clipBit(good, 2);
	std::vector<uint16_t> trunc(good.begin(), good.begin() + 5);
	std::vector<uint16_t> burst = burstOf({ bad, good, good, trunc }, 12896);
	uint16_t out[256];
	uint16_t gap = 0;
	size_t n = declipBurst(burst.data(), burst.size(), out, 256, &gap);
	TEST_ASSERT_EQUAL_UINT32(good.size(), n);
	TEST_ASSERT_EQUAL_UINT16_ARRAY(good.data(), out, n);
}

// The stored reset gap is the longest pause, not an average: a noise-shortened
// gap between two frames never drags the measured gap down.
static void test_declip_gap_is_longest() {
	std::vector<uint16_t> good = pt2262Frame({ 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1 });
	std::vector<uint16_t> burst;
	burst.insert(burst.end(), good.begin(), good.end());
	burst.push_back(6000);  // a chopped gap
	burst.insert(burst.end(), good.begin(), good.end());
	burst.push_back(12896);  // the true gap
	burst.insert(burst.end(), good.begin(), good.end());
	uint16_t out[256];
	uint16_t gap = 0;
	size_t n = declipBurst(burst.data(), burst.size(), out, 256, &gap);
	TEST_ASSERT_EQUAL_UINT32(good.size(), n);
	TEST_ASSERT_EQUAL_UINT16(12896, gap);
}

// A multi-frame RF press de-clips to one clean frame + its reset gap, with
// frameRepeat set so transmit replays it as a continuous burst.
static void test_rf_capture_declips_and_sets_repeat() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> good = pt2262Frame({ 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1 });
	std::vector<uint16_t> bad = clipBit(good, 0);
	std::vector<uint16_t> burst = burstOf({ bad, good, bad, good }, 12896);
	uint64_t end = feedAfter(m, Source::Rf433, burst, 0, T0, 5);
	end = feedAfter(m, Source::Rf433, burst, 0, end, 12);
	end = feedAfter(m, Source::Rf433, burst, 0, end, 12);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));
	feedAfter(m, Source::Rf433, burst, 0, end, 400);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));

	StoredCode code = m.takeCode();
	TEST_ASSERT_EQUAL(static_cast<int>(CodeKind::RF), static_cast<int>(code.kind));
	TEST_ASSERT_EQUAL_UINT16(433, code.freqMHz);
	TEST_ASSERT_EQUAL_UINT32(good.size() + 1, code.pulses.size());  // one frame + gap
	for (size_t i = 0; i < good.size(); ++i)
		TEST_ASSERT_EQUAL_UINT16(good[i], code.pulses[i]);  // de-clipped frame
	TEST_ASSERT_EQUAL_UINT16(12896, code.pulses.back());     // reset gap
	TEST_ASSERT_EQUAL_UINT8(4, code.frameRepeat);            // measured: 4 frames in the burst
}

// When ambient traffic has filled every cluster slot, a real repeating remote
// still captures: the weakest (one-off) slot is evicted to make room.
static void test_ambient_eviction_lets_remote_capture() {
	LearnMachine m;
	m.begin(T0);
	uint64_t end = T0;
	for (size_t k = 0; k < LearnMachine::kMaxClusters; ++k)
		end = feedAfter(m, Source::Rf433, noiseFrame(17 + k * 2), 0, end, 50);
	std::vector<uint16_t> remote = rfFrame(0x0ABCDE);
	end = feedAfter(m, Source::Rf433, remote, 0, end, 400);
	end = feedAfter(m, Source::Rf433, remote, 0, end, 12);
	end = feedAfter(m, Source::Rf433, remote, 0, end, 12);
	feedAfter(m, Source::Rf433, remote, 0, end, 400);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
}

// Once a remote has been pressed twice, an ambient flood that ties its match
// count must not evict it before the third press locks it in. Without the
// seen-across-presses protection the remote (oldest slot) loses the tie.
static void test_remote_survives_ambient_after_two_presses() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> remote = rfFrame(0x0ABCDE);
	uint64_t end = feedAfter(m, Source::Rf433, remote, 0, T0, 400);  // press 1
	end = feedAfter(m, Source::Rf433, remote, 0, end, 400);          // press 2 -> protected
	// Ambient signals, each repeating within its own window so its count ties the
	// remote's (without protection the remote would lose the tie and be evicted).
	for (size_t k = 0; k < LearnMachine::kMaxClusters + 2; ++k) {
		std::vector<uint16_t> amb = noiseFrame(17 + k * 2);
		end = feedAfter(m, Source::Rf433, amb, 0, end, 400);  // new press
		end = feedAfter(m, Source::Rf433, amb, 0, end, 5);    // repeat -> count 2
	}
	feedAfter(m, Source::Rf433, remote, 0, end, 400);  // press 3 -> captures if it survived
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
}

// --- learn machine: success paths ---

// Three jittered IR repeats in one press capture; the stored code is the
// per-entry average with the measured inter-repeat gap appended.
static void test_ir_success_single_press() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> base = irFrame();
	uint64_t end = feedAfter(m, Source::Ir, base, 38000, T0, 5);
	end = feedAfter(m, Source::Ir, jitter(base, 30), 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));
	feedAfter(m, Source::Ir, base, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));

	StoredCode code = m.takeCode();
	TEST_ASSERT_EQUAL(static_cast<int>(State::Idle), static_cast<int>(m.state()));
	TEST_ASSERT_TRUE(code.isLearned());
	TEST_ASSERT_EQUAL(static_cast<int>(CodeKind::IR), static_cast<int>(code.kind));
	TEST_ASSERT_EQUAL_UINT16(38000, code.carrierHz);
	TEST_ASSERT_EQUAL_UINT32(base.size() + 1, code.pulses.size());
	// average of (v, v + 30, v) rounds to v + 10
	TEST_ASSERT_EQUAL_UINT16(base[0] + 10, code.pulses[0]);
	TEST_ASSERT_EQUAL_UINT16(base[1] + 10, code.pulses[1]);
	// both observed inter-repeat gaps were 40 ms
	TEST_ASSERT_EQUAL_UINT16(40000, code.pulses.back());
}

// RF needs a second press: three same-press matches keep listening, a fourth
// matching burst in a new press captures.
static void test_rf_success_needs_two_presses() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> f = rfFrame(0xABC123);
	uint64_t end = feedAfter(m, Source::Rf433, f, 0, T0, 5);
	end = feedAfter(m, Source::Rf433, f, 0, end, 12);
	end = feedAfter(m, Source::Rf433, f, 0, end, 12);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));
	feedAfter(m, Source::Rf433, f, 0, end, 400);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));

	StoredCode code = m.takeCode();
	TEST_ASSERT_EQUAL(static_cast<int>(CodeKind::RF), static_cast<int>(code.kind));
	TEST_ASSERT_EQUAL_UINT16(433, code.freqMHz);
	TEST_ASSERT_EQUAL_UINT16(0, code.carrierHz);
	TEST_ASSERT_FALSE(code.rolling);
	TEST_ASSERT_EQUAL_UINT32(f.size() + 1, code.pulses.size());  // one frame + gap
	TEST_ASSERT_EQUAL_UINT16(12000, code.pulses.back());
	TEST_ASSERT_EQUAL_UINT8(kStoredFrameCount, code.frameRepeat);  // measured repeat
}

// The 315 source stamps its band on the stored code.
static void test_rf315_band_mapped() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> f = rfFrame(0x00F0F0);
	uint64_t end = feedAfter(m, Source::Rf315, f, 0, T0, 5);
	end = feedAfter(m, Source::Rf315, f, 0, end, 12);
	feedAfter(m, Source::Rf315, f, 0, end, 400);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
	TEST_ASSERT_EQUAL_UINT16(315, m.takeCode().freqMHz);
}

// Single-burst presses leave no measurable inter-repeat gap; the default
// trailing gap is appended instead.
static void test_default_trailing_gap() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> f = rfFrame(0x0A0A0A);
	uint64_t end = feedAfter(m, Source::Rf433, f, 0, T0, 5);
	end = feedAfter(m, Source::Rf433, f, 0, end, 400);
	feedAfter(m, Source::Rf433, f, 0, end, 400);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
	TEST_ASSERT_EQUAL_UINT16(LearnMachine::kDefaultTrailGapUs, m.takeCode().pulses.back());
}

// An even-length burst is stored as-is, no gap appended.
static void test_even_length_burst_kept() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> f(16, 560);
	uint64_t end = feedAfter(m, Source::Ir, f, 38000, T0, 5);
	end = feedAfter(m, Source::Ir, f, 38000, end, 40);
	feedAfter(m, Source::Ir, f, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
	TEST_ASSERT_EQUAL_UINT32(16, m.takeCode().pulses.size());
}

// A burst at the length ceiling still captures and fits the config bound.
static void test_max_length_burst() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> f(kMaxBurstPulses, 560);
	uint64_t end = feedAfter(m, Source::Ir, f, 38000, T0, 5);
	end = feedAfter(m, Source::Ir, f, 38000, end, 40);
	feedAfter(m, Source::Ir, f, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
	TEST_ASSERT_EQUAL_UINT32(MAX_PULSES, m.takeCode().pulses.size());
}

// --- learn machine: rejection and classification ---

// No bursts at all: timeout. The deadline is begin + window, edge-inclusive.
static void test_timeout_when_silent() {
	LearnMachine m;
	m.begin(T0);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening),
	                  static_cast<int>(m.tick(T0 + LearnMachine::kWindowMs - 1)));
	TEST_ASSERT_EQUAL(static_cast<int>(State::Failed),
	                  static_cast<int>(m.tick(T0 + LearnMachine::kWindowMs)));
	TEST_ASSERT_EQUAL(static_cast<int>(FailReason::Timeout), static_cast<int>(m.failReason()));
}

// Activity that never produces three matches: noisy, not timeout. Three
// distinct payloads in ONE press never read as rolling either.
static void test_noisy_when_no_match() {
	LearnMachine m;
	m.begin(T0);
	uint64_t end = feedAfter(m, Source::Rf433, rfFrame(1), 0, T0, 5);
	end = feedAfter(m, Source::Rf433, rfFrame(2), 0, end, 12);
	feedAfter(m, Source::Rf433, rfFrame(3), 0, end, 12);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Failed),
	                  static_cast<int>(m.tick(T0 + LearnMachine::kWindowMs)));
	TEST_ASSERT_EQUAL(static_cast<int>(FailReason::Noisy), static_cast<int>(m.failReason()));
}

// Malformed-only activity (too short) is still activity, so noisy.
static void test_noisy_on_malformed_activity() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> ditto{ 9000, 2250, 560 };
	feedAfter(m, Source::Ir, ditto, 38000, T0, 5);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Failed),
	                  static_cast<int>(m.tick(T0 + LearnMachine::kWindowMs)));
	TEST_ASSERT_EQUAL(static_cast<int>(FailReason::Noisy), static_cast<int>(m.failReason()));
}

// A held rolling fob repeats one word inside one press: never a capture
// (would be spent on replay), and one press alone never proves rolling.
static void test_held_rolling_fob_not_saved() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> f = rfFrame(0x5A5A5A);
	uint64_t end = feedAfter(m, Source::Rf433, f, 0, T0, 5);
	end = feedAfter(m, Source::Rf433, f, 0, end, 12);
	feedAfter(m, Source::Rf433, f, 0, end, 12);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));
	TEST_ASSERT_EQUAL(static_cast<int>(State::Failed),
	                  static_cast<int>(m.tick(T0 + LearnMachine::kWindowMs)));
	TEST_ASSERT_EQUAL(static_cast<int>(FailReason::Noisy), static_cast<int>(m.failReason()));
}

// Three presses, three structurally alike but distinct payloads: rolling.
static void test_rolling_three_distinct_presses() {
	LearnMachine m;
	m.begin(T0);
	uint64_t end = feedAfter(m, Source::Rf433, rfFrame(0x111111), 0, T0, 5);
	end = feedAfter(m, Source::Rf433, rfFrame(0x222222), 0, end, 500);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));
	feedAfter(m, Source::Rf433, rfFrame(0x333333), 0, end, 500);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Failed), static_cast<int>(m.state()));
	TEST_ASSERT_EQUAL(static_cast<int>(FailReason::Rolling), static_cast<int>(m.failReason()));
}

// Alternating two buttons of a fixed-code remote is not rolling; the
// repeated payload eventually captures.
static void test_two_buttons_alternating_not_rolling() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> a = rfFrame(0x00F0F0);
	std::vector<uint16_t> b = rfFrame(0x0F0F00);
	uint64_t end = feedAfter(m, Source::Rf433, a, 0, T0, 5);
	end = feedAfter(m, Source::Rf433, b, 0, end, 400);
	end = feedAfter(m, Source::Rf433, a, 0, end, 400);
	end = feedAfter(m, Source::Rf433, b, 0, end, 400);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));
	feedAfter(m, Source::Rf433, a, 0, end, 400);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
}

// A payload repeated across presses blocks the rolling verdict even when
// other presses differ; one more matching press then captures.
static void test_repeated_payload_blocks_rolling() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> a = rfFrame(0x123456);
	uint64_t end = feedAfter(m, Source::Rf433, a, 0, T0, 5);
	end = feedAfter(m, Source::Rf433, a, 0, end, 400);
	end = feedAfter(m, Source::Rf433, rfFrame(0x654321), 0, end, 400);
	end = feedAfter(m, Source::Rf433, rfFrame(0x0FF0FF), 0, end, 400);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));
	feedAfter(m, Source::Rf433, a, 0, end, 400);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
}

// An out-of-tolerance burst forms its own cluster and never pollutes the
// matching one; the stored code averages only true matches.
static void test_outlier_clusters_separately() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> base = irFrame();
	std::vector<uint16_t> outlier = base;
	outlier[0] = static_cast<uint16_t>(outlier[0] * 2);
	uint64_t end = feedAfter(m, Source::Ir, base, 38000, T0, 5);
	end = feedAfter(m, Source::Ir, outlier, 38000, end, 40);
	end = feedAfter(m, Source::Ir, base, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));
	feedAfter(m, Source::Ir, base, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
	TEST_ASSERT_EQUAL_UINT16(base[0], m.takeCode().pulses[0]);
}

// Short IR ditto frames between full frames never match and never block the
// full frames from capturing.
static void test_dittos_do_not_interfere() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> full = irFrame();
	std::vector<uint16_t> ditto{ 9000, 2250, 560 };
	uint64_t end = feedAfter(m, Source::Ir, full, 38000, T0, 5);
	end = feedAfter(m, Source::Ir, ditto, 38000, end, 40);
	end = feedAfter(m, Source::Ir, full, 38000, end, 40);
	end = feedAfter(m, Source::Ir, ditto, 38000, end, 40);
	feedAfter(m, Source::Ir, full, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
}

// Sub-minimum and zero-duration bursts never cluster.
static void test_malformed_bursts_never_match() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> tiny(kMinBurstPulses - 1, 560);
	uint64_t end = feedAfter(m, Source::Ir, tiny, 38000, T0, 5);
	end = feedAfter(m, Source::Ir, tiny, 38000, end, 40);
	feedAfter(m, Source::Ir, tiny, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));

	m.begin(T0);
	std::vector<uint16_t> zeroed = irFrame();
	zeroed[5] = 0;
	end = feedAfter(m, Source::Ir, zeroed, 38000, T0, 5);
	end = feedAfter(m, Source::Ir, zeroed, 38000, end, 40);
	feedAfter(m, Source::Ir, zeroed, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));

	m.begin(T0);
	std::vector<uint16_t> tooLong(kMaxBurstPulses + 1, 560);
	end = feedAfter(m, Source::Ir, tooLong, 38000, T0, 5);
	end = feedAfter(m, Source::Ir, tooLong, 38000, end, 40);
	feedAfter(m, Source::Ir, tooLong, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));
}

// The learn carrier window agrees with config validation: a code captured at
// the edge passes the config layer, one outside never captures at all.
static void test_carrier_bounds_agree_with_validation() {
	for (uint16_t bad : { static_cast<uint16_t>(kIrCarrierMinHz - 1),
	                      static_cast<uint16_t>(kIrCarrierMaxHz + 1) }) {
		LearnMachine m;
		m.begin(T0);
		std::vector<uint16_t> f = irFrame();
		uint64_t end = feedAfter(m, Source::Ir, f, bad, T0, 5);
		end = feedAfter(m, Source::Ir, f, bad, end, 40);
		feedAfter(m, Source::Ir, f, bad, end, 40);
		TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));
	}
	for (uint16_t edge : { kIrCarrierMinHz, kIrCarrierMaxHz }) {
		LearnMachine m;
		m.begin(T0);
		std::vector<uint16_t> f = irFrame();
		uint64_t end = feedAfter(m, Source::Ir, f, edge, T0, 5);
		end = feedAfter(m, Source::Ir, f, edge, end, 40);
		feedAfter(m, Source::Ir, f, edge, end, 40);
		TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
		StoredCode code = m.takeCode();
		TEST_ASSERT_EQUAL_UINT16(edge, code.carrierHz);

		MemoryBlobStore store;
		Config cfg;
		AddDeviceResult r = addDevice(store, cfg, "TV", "Television", "power", code);
		TEST_ASSERT_EQUAL(static_cast<int>(MutateError::Ok), static_cast<int>(r.error));
	}
}

// --- learn machine: session lifecycle ---

// Bursts are ignored unless Listening: before begin, and after a capture.
static void test_feed_outside_listening_ignored() {
	LearnMachine m;
	std::vector<uint16_t> f = irFrame();
	m.feedBurst(Source::Ir, f.data(), f.size(), 38000, T0);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Idle), static_cast<int>(m.state()));

	// pre-begin activity must not turn a silent window into noisy
	m.begin(T0);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Failed),
	                  static_cast<int>(m.tick(T0 + LearnMachine::kWindowMs)));
	TEST_ASSERT_EQUAL(static_cast<int>(FailReason::Timeout), static_cast<int>(m.failReason()));

	m.begin(T0);
	uint64_t end = feedAfter(m, Source::Ir, f, 38000, T0, 5);
	end = feedAfter(m, Source::Ir, f, 38000, end, 40);
	end = feedAfter(m, Source::Ir, f, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
	feedAfter(m, Source::Ir, f, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
	TEST_ASSERT_TRUE(m.takeCode().isLearned());
}

// Cancel drops the session and any captured code.
static void test_cancel_drops_session() {
	LearnMachine m;
	m.begin(T0);
	TEST_ASSERT_TRUE(m.active());
	std::vector<uint16_t> f = irFrame();
	feedAfter(m, Source::Ir, f, 38000, T0, 5);
	m.cancel();
	TEST_ASSERT_EQUAL(static_cast<int>(State::Idle), static_cast<int>(m.state()));
	TEST_ASSERT_FALSE(m.active());
	TEST_ASSERT_FALSE(m.takeCode().isLearned());
}

// begin during a session re-arms from scratch; prior bursts never count.
static void test_begin_resets_session() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> f = irFrame();
	uint64_t end = feedAfter(m, Source::Ir, f, 38000, T0, 5);
	feedAfter(m, Source::Ir, f, 38000, end, 40);

	m.begin(T0 + 5000);
	end = feedAfter(m, Source::Ir, f, 38000, T0 + 5000, 5);
	feedAfter(m, Source::Ir, f, 38000, end, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Listening), static_cast<int>(m.state()));
	feedAfter(m, Source::Ir, f, 38000, end + durMs(f) + 40, 40);
	TEST_ASSERT_EQUAL(static_cast<int>(State::Captured), static_cast<int>(m.state()));
}

// takeCode outside Captured returns an unlearned code.
static void test_take_code_requires_capture() {
	LearnMachine m;
	TEST_ASSERT_FALSE(m.takeCode().isLearned());
	m.begin(T0);
	TEST_ASSERT_FALSE(m.takeCode().isLearned());
}

// --- config mutation ---

// Adds a fresh command and persists it; a reload sees both commands.
static void test_upsert_adds_command() {
	MemoryBlobStore store;
	Config cfg;
	AddDeviceResult r = addDevice(store, cfg, "Screen", "WindowCovering", "up",
	                              makeRf(315, { 300, 900, 300, 900 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::Ok), static_cast<int>(r.error));

	MutateError e = upsertCommand(store, cfg, r.id, "down",
	                              makeRf(315, { 900, 300, 900, 300 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::Ok), static_cast<int>(e));
	TEST_ASSERT_EQUAL_UINT32(2, cfg.devices[0].commands.size());

	DecodeResult loaded = load(store);
	TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Ok), static_cast<int>(loaded.status));
	TEST_ASSERT_EQUAL_UINT32(2, loaded.config.devices[0].commands.size());
	TEST_ASSERT_EQUAL_STRING("down", loaded.config.devices[0].commands[1].name.c_str());
}

// A same-named command is replaced, not duplicated.
static void test_upsert_replaces_command() {
	MemoryBlobStore store;
	Config cfg;
	AddDeviceResult r = addDevice(store, cfg, "Screen", "WindowCovering", "up",
	                              makeRf(315, { 300, 900, 300, 900 }));
	MutateError e = upsertCommand(store, cfg, r.id, "up",
	                              makeRf(433, { 500, 700, 500, 700 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::Ok), static_cast<int>(e));
	TEST_ASSERT_EQUAL_UINT32(1, cfg.devices[0].commands.size());
	TEST_ASSERT_EQUAL_UINT16(433, cfg.devices[0].commands[0].code.freqMHz);
}

// Unknown device id fails without touching anything.
static void test_upsert_device_not_found() {
	MemoryBlobStore store;
	Config cfg;
	addDevice(store, cfg, "Screen", "WindowCovering", "up", makeRf(315, { 300, 900, 300, 900 }));
	MutateError e = upsertCommand(store, cfg, 99, "down", makeRf(315, { 900, 300, 900, 300 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::DeviceNotFound), static_cast<int>(e));
	TEST_ASSERT_EQUAL_UINT32(1, cfg.devices[0].commands.size());
}

// A code the config layer rejects leaves cfg and the stored blob untouched.
static void test_upsert_invalid_leaves_untouched() {
	MemoryBlobStore store;
	Config cfg;
	AddDeviceResult r = addDevice(store, cfg, "Screen", "WindowCovering", "up",
	                              makeRf(315, { 300, 900, 300, 900 }));
	MutateError e = upsertCommand(store, cfg, r.id, "bad",
	                              makeRf(315, { 300, 900, 300 }));  // odd pulse count
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::FailedValidation), static_cast<int>(e));
	TEST_ASSERT_EQUAL_UINT32(1, cfg.devices[0].commands.size());
	DecodeResult loaded = load(store);
	TEST_ASSERT_EQUAL_UINT32(1, loaded.config.devices[0].commands.size());
}

// A store that rejects the write fails the mutation and changes nothing.
static void test_upsert_store_rejected() {
	MemoryBlobStore store;
	Config cfg;
	AddDeviceResult r = addDevice(store, cfg, "Screen", "WindowCovering", "up",
	                              makeRf(315, { 300, 900, 300, 900 }));
	MemoryBlobStore full(0);
	MutateError e = upsertCommand(full, cfg, r.id, "down", makeRf(315, { 900, 300, 900, 300 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::StoreRejected), static_cast<int>(e));
	TEST_ASSERT_EQUAL_UINT32(1, cfg.devices[0].commands.size());
}

// Ids come from nextDeviceId and only grow.
static void test_add_device_monotonic_ids() {
	MemoryBlobStore store;
	Config cfg;
	AddDeviceResult a = addDevice(store, cfg, "A", "Switch", "on", makeRf(433, { 300, 900, 300, 900 }));
	AddDeviceResult b = addDevice(store, cfg, "B", "Fan", "on", makeRf(433, { 400, 800, 400, 800 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::Ok), static_cast<int>(a.error));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::Ok), static_cast<int>(b.error));
	TEST_ASSERT_EQUAL_UINT16(2, a.id);
	TEST_ASSERT_EQUAL_UINT16(3, b.id);
	TEST_ASSERT_EQUAL_UINT16(4, cfg.nextDeviceId);
	TEST_ASSERT_EQUAL_UINT32(2, cfg.devices.size());
}

// The device ceiling rejects the 33rd device.
static void test_add_device_limit() {
	MemoryBlobStore store;
	Config cfg;
	for (size_t i = 0; i < MAX_DEVICES; ++i) {
		AddDeviceResult r = addDevice(store, cfg, "Device " + std::to_string(i), "Switch",
		                              "on", makeRf(433, { 300, 900, 300, 900 }));
		TEST_ASSERT_EQUAL(static_cast<int>(MutateError::Ok), static_cast<int>(r.error));
	}
	AddDeviceResult r = addDevice(store, cfg, "Extra", "Switch", "on",
	                              makeRf(433, { 300, 900, 300, 900 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::DeviceLimit), static_cast<int>(r.error));
	TEST_ASSERT_EQUAL_UINT32(MAX_DEVICES, cfg.devices.size());
}

// Only the six supported service strings create devices.
static void test_add_device_bad_service() {
	MemoryBlobStore store;
	Config cfg;
	AddDeviceResult r = addDevice(store, cfg, "Toaster", "Toaster", "on",
	                              makeRf(433, { 300, 900, 300, 900 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::BadService), static_cast<int>(r.error));
	TEST_ASSERT_TRUE(cfg.devices.empty());
}

// Names must be non-empty and within the shared ceiling.
static void test_name_bounds() {
	MemoryBlobStore store;
	Config cfg;
	AddDeviceResult r = addDevice(store, cfg, "", "Switch", "on",
	                              makeRf(433, { 300, 900, 300, 900 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::BadName), static_cast<int>(r.error));

	r = addDevice(store, cfg, "TV", "Television", std::string(MAX_NAME_LEN + 1, 'a'),
	              makeRf(433, { 300, 900, 300, 900 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::BadName), static_cast<int>(r.error));

	r = addDevice(store, cfg, std::string(MAX_NAME_LEN, 'a'), "Television", "power",
	              makeRf(433, { 300, 900, 300, 900 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::Ok), static_cast<int>(r.error));

	MutateError e = upsertCommand(store, cfg, r.id, "", makeRf(433, { 300, 900, 300, 900 }));
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::BadName), static_cast<int>(e));
}

// Full pipeline: a captured code lands in a new device and survives a
// store round trip bit-for-bit.
static void test_learned_code_roundtrip() {
	LearnMachine m;
	m.begin(T0);
	std::vector<uint16_t> f = rfFrame(0xABCDEF);
	uint64_t end = feedAfter(m, Source::Rf315, f, 0, T0, 5);
	end = feedAfter(m, Source::Rf315, f, 0, end, 12);
	feedAfter(m, Source::Rf315, f, 0, end, 400);
	StoredCode code = m.takeCode();
	TEST_ASSERT_TRUE(code.isLearned());

	MemoryBlobStore store;
	Config cfg;
	AddDeviceResult r = addDevice(store, cfg, "Fan", "Fan", "power", code);
	TEST_ASSERT_EQUAL(static_cast<int>(MutateError::Ok), static_cast<int>(r.error));

	DecodeResult loaded = load(store);
	TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Ok), static_cast<int>(loaded.status));
	const StoredCode& back = loaded.config.devices[0].commands[0].code;
	TEST_ASSERT_EQUAL(static_cast<int>(CodeKind::RF), static_cast<int>(back.kind));
	TEST_ASSERT_EQUAL_UINT16(315, back.freqMHz);
	TEST_ASSERT_EQUAL_UINT32(code.pulses.size(), back.pulses.size());
	TEST_ASSERT_EQUAL_UINT16_ARRAY(code.pulses.data(), back.pulses.data(), code.pulses.size());
}

int main(int, char**) {
	UNITY_BEGIN();
	RUN_TEST(test_duration_tolerance);
	RUN_TEST(test_declip_single_frame_passthrough);
	RUN_TEST(test_declip_recovers_clipped_float);
	RUN_TEST(test_declip_drops_ragged_frames);
	RUN_TEST(test_declip_gap_is_longest);
	RUN_TEST(test_rf_capture_declips_and_sets_repeat);
	RUN_TEST(test_ambient_eviction_lets_remote_capture);
	RUN_TEST(test_remote_survives_ambient_after_two_presses);
	RUN_TEST(test_ir_success_single_press);
	RUN_TEST(test_rf_success_needs_two_presses);
	RUN_TEST(test_rf315_band_mapped);
	RUN_TEST(test_default_trailing_gap);
	RUN_TEST(test_even_length_burst_kept);
	RUN_TEST(test_max_length_burst);
	RUN_TEST(test_timeout_when_silent);
	RUN_TEST(test_noisy_when_no_match);
	RUN_TEST(test_noisy_on_malformed_activity);
	RUN_TEST(test_held_rolling_fob_not_saved);
	RUN_TEST(test_rolling_three_distinct_presses);
	RUN_TEST(test_two_buttons_alternating_not_rolling);
	RUN_TEST(test_repeated_payload_blocks_rolling);
	RUN_TEST(test_outlier_clusters_separately);
	RUN_TEST(test_dittos_do_not_interfere);
	RUN_TEST(test_malformed_bursts_never_match);
	RUN_TEST(test_carrier_bounds_agree_with_validation);
	RUN_TEST(test_feed_outside_listening_ignored);
	RUN_TEST(test_cancel_drops_session);
	RUN_TEST(test_begin_resets_session);
	RUN_TEST(test_take_code_requires_capture);
	RUN_TEST(test_upsert_adds_command);
	RUN_TEST(test_upsert_replaces_command);
	RUN_TEST(test_upsert_device_not_found);
	RUN_TEST(test_upsert_invalid_leaves_untouched);
	RUN_TEST(test_upsert_store_rejected);
	RUN_TEST(test_add_device_monotonic_ids);
	RUN_TEST(test_add_device_limit);
	RUN_TEST(test_add_device_bad_service);
	RUN_TEST(test_name_bounds);
	RUN_TEST(test_learned_code_roundtrip);
	return UNITY_END();
}
