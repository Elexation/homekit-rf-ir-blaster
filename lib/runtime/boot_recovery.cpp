#include "boot_recovery.h"

namespace runtime {

BootDecision evaluateBoot(uint8_t persistedCount, uint32_t bootNowMs) {
	uint8_t newCount = persistedCount < kMaxBootCount
		? static_cast<uint8_t>(persistedCount + 1)
		: kMaxBootCount;  // any garbage at or above the cap saturates, never wraps
	BootDecision d;
	d.newCount = newCount;
	d.enterSafeMode = newCount >= kSafeModeBootThreshold;
	d.clearAtMs = bootNowMs + kStableRunMs;
	return d;
}

}  // namespace runtime
