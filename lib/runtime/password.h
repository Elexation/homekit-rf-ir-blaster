#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "crypto.h"

namespace runtime {

// Builds the record pbkdf2_sha256$<iterations>$<base64url(salt)>$<base64url(dk)>.
std::string deriveCredential(const Crypto& crypto, const std::string& password,
                             const uint8_t* salt, size_t saltLen, uint32_t iterations);

// Constant-time verify; false on a wrong password or any malformed record.
bool verifyPassword(const Crypto& crypto, const std::string& password, const std::string& record);

}  // namespace runtime
