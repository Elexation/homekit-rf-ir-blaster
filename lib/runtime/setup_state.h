#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace runtime {

enum class SetupMode {
	AwaitingSetup,
	Configured,
};

// Configured once a credential exists; until then every route forces setup.
SetupMode currentMode(bool credentialPresent);

// Allowed pre-credential only, via a matching nonce or an open button window (now < buttonWindowUntil, ms since boot).
bool setupAllowed(bool credentialPresent, const std::string& providedNonce,
                  const std::string& storedNonce, uint64_t buttonWindowUntil, uint64_t now);

// One-time setup nonce; device supplies the random bytes and prints it to serial.
std::string makeNonce(const uint8_t* rand, size_t randLen);

}  // namespace runtime
