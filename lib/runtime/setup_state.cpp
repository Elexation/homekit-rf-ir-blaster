#include "setup_state.h"

#include "crypto.h"
#include "encoding.h"

namespace runtime {

SetupMode currentMode(bool credentialPresent) {
	return credentialPresent ? SetupMode::Configured : SetupMode::AwaitingSetup;
}

bool setupAllowed(bool credentialPresent, const std::string& providedNonce,
                  const std::string& storedNonce, uint64_t buttonWindowUntil, uint64_t now) {
	if (credentialPresent)
		return false;
	// Empty-stored guard blocks an empty==empty nonce bypass.
	if (!storedNonce.empty() && constantTimeEqual(providedNonce, storedNonce))
		return true;
	return now < buttonWindowUntil;
}

std::string makeNonce(const uint8_t* rand, size_t randLen) {
	return base64urlEncode(rand, randLen);
}

}  // namespace runtime
