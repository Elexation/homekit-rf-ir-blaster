#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "crypto.h"

namespace runtime {

// Signed double-submit CSRF token base64url(rand).base64url(hmac), hmac over binding + "!" + rand.
// binding is the session's "user|iat" (stable across refreshes), so the token survives idle slides.
std::string mintCsrf(const Crypto& crypto, const std::string& secret, const std::string& binding,
                     const uint8_t* rand, size_t randLen);

// True only if the cookie and form tokens are byte-identical and the signature binds to `binding`.
bool verifyCsrf(const Crypto& crypto, const std::string& secret, const std::string& binding,
                const std::string& cookieTok, const std::string& formTok);

}  // namespace runtime
