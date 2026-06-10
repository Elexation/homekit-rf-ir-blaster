#pragma once

#include <cstdint>
#include <string>

#include "crypto.h"

namespace runtime {

struct SessionResult {
	bool        valid = false;
	std::string user;
	std::string binding;  // "user|iat"; stable across refreshes (CSRF binds to it)
	bool        expiredIdle = false;
	bool        expiredAbsolute = false;
};

// Mints base64url(user|iat|absExpiry|idleDeadline).base64url(hmac); iat is ms since boot.
// Empty string if user is empty or contains '|'.
std::string mintSession(const Crypto& crypto, const std::string& secret,
                        const std::string& user, uint64_t iat);

// Authenticates the token, then reports expiry against `now` (ms since boot).
SessionResult verifySession(const Crypto& crypto, const std::string& secret,
                            const std::string& token, uint64_t now);

// Re-issues with the idle deadline slid to now + timeout, keeping iat and the absolute cap.
// Empty if the token is forged, malformed, or already expired.
std::string refreshSession(const Crypto& crypto, const std::string& secret,
                           const std::string& token, uint64_t now);

}  // namespace runtime
