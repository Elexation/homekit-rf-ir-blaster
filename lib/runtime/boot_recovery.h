#pragma once

#include <cstdint>

namespace runtime {

// Boot-loop recovery decision logic: pure and total over all inputs (no clock, no
// flash, no GPIO; that device seam is wired separately). Device usage: read the persisted
// boot count, call evaluateBoot(count, millis()), persist newCount; then in loop(), the
// first time shouldClearCounter(millis(), clearAtMs) is true, persist 0 (a healthy run).

constexpr uint8_t  kSafeModeBootThreshold = 3;
constexpr uint32_t kStableRunMs           = 10u * 1000u;  // 10 seconds
constexpr uint8_t  kMaxBootCount          = 250;          // saturating; no wrap

struct BootDecision {
	uint8_t  newCount;
	bool     enterSafeMode;
	uint32_t clearAtMs;
};

BootDecision evaluateBoot(uint8_t persistedCount, uint32_t bootNowMs);

inline bool shouldClearCounter(uint32_t nowMs, uint32_t clearAtMs) {
	return nowMs >= clearAtMs;
}

}  // namespace runtime
