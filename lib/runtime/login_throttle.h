#pragma once

#include <cstdint>

namespace runtime {

// Per-client login throttle: pure and time-injected (the caller passes a monotonic
// `now` in ms; this never reads a clock). `clientKey` is abstract: a per-IP hash or a
// single global key, the caller's choice. The u32 ms space wraps about every 49.7 days;
// a wrap only ends a lockout early or extends it one cycle, both self-healing, so accepted.
class LoginThrottle {
public:
	static constexpr uint8_t  kMaxFailures       = 5;
	static constexpr uint32_t kLockoutMs         = 15u * 60u * 1000u;  // 15 minutes
	static constexpr uint8_t  kMaxTrackedClients = 16;

	bool     isLocked(uint32_t clientKey, uint32_t now) const;
	uint32_t timeRemainingMs(uint32_t clientKey, uint32_t now) const;
	void     recordFailure(uint32_t clientKey, uint32_t now);
	void     recordSuccess(uint32_t clientKey, uint32_t now);

private:
	struct Entry {
		uint32_t clientKey   = 0;
		uint8_t  failures    = 0;
		uint32_t lockedUntil = 0;  // 0 = not locked
		uint32_t lastSeenMs  = 0;
		bool     used        = false;
	};

	const Entry* find(uint32_t clientKey) const;
	Entry*       find(uint32_t clientKey);
	Entry*       allocate(uint32_t clientKey, uint32_t now);

	Entry entries_[kMaxTrackedClients];
};

}  // namespace runtime
