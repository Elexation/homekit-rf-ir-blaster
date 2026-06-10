#include "login_throttle.h"

namespace runtime {

const LoginThrottle::Entry* LoginThrottle::find(uint32_t clientKey) const {
	for (const Entry& e : entries_)
		if (e.used && e.clientKey == clientKey)
			return &e;
	return nullptr;
}

LoginThrottle::Entry* LoginThrottle::find(uint32_t clientKey) {
	for (Entry& e : entries_)
		if (e.used && e.clientKey == clientKey)
			return &e;
	return nullptr;
}

// Reuse a free slot if any; otherwise evict the oldest UNLOCKED entry so an attacker
// can't flush a victim's lockout by spraying fresh keys. Fall back to global LRU only
// when every slot is locked.
LoginThrottle::Entry* LoginThrottle::allocate(uint32_t clientKey, uint32_t now) {
	Entry* oldestUnlocked = nullptr;
	Entry* oldestAny = nullptr;
	for (Entry& e : entries_) {
		if (!e.used) {
			e = Entry{};
			e.clientKey = clientKey;
			e.used = true;
			e.lastSeenMs = now;
			return &e;
		}
		bool locked = e.lockedUntil != 0 && now < e.lockedUntil;
		if (!locked && (!oldestUnlocked || e.lastSeenMs < oldestUnlocked->lastSeenMs))
			oldestUnlocked = &e;
		if (!oldestAny || e.lastSeenMs < oldestAny->lastSeenMs)
			oldestAny = &e;
	}
	Entry* victim = oldestUnlocked ? oldestUnlocked : oldestAny;
	*victim = Entry{};
	victim->clientKey = clientKey;
	victim->used = true;
	victim->lastSeenMs = now;
	return victim;
}

bool LoginThrottle::isLocked(uint32_t clientKey, uint32_t now) const {
	const Entry* e = find(clientKey);
	if (!e || e->lockedUntil == 0)
		return false;
	return now < e->lockedUntil;
}

uint32_t LoginThrottle::timeRemainingMs(uint32_t clientKey, uint32_t now) const {
	const Entry* e = find(clientKey);
	if (!e || e->lockedUntil == 0 || now >= e->lockedUntil)
		return 0;  // clamp; an expired or backward clock yields no remaining time
	return e->lockedUntil - now;
}

void LoginThrottle::recordFailure(uint32_t clientKey, uint32_t now) {
	Entry* e = find(clientKey);
	if (!e)
		e = allocate(clientKey, now);
	e->lastSeenMs = now;

	if (e->lockedUntil != 0) {
		if (now < e->lockedUntil)
			return;  // active lockout: never extend, never shorten
		e->failures = 0;
		e->lockedUntil = 0;
	}
	if (e->failures < 0xFF)
		++e->failures;
	if (e->failures >= kMaxFailures)
		e->lockedUntil = now + kLockoutMs;
}

void LoginThrottle::recordSuccess(uint32_t clientKey, uint32_t now) {
	Entry* e = find(clientKey);
	if (!e)
		return;  // untracked client: nothing to reset, and don't burn a slot
	e->lastSeenMs = now;
	if (e->lockedUntil != 0 && now < e->lockedUntil)
		return;  // lockout wins: a success during an active lockout changes nothing else
	e->failures = 0;
	e->lockedUntil = 0;
}

}  // namespace runtime
