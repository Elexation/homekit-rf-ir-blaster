#pragma once

#include "crypto.h"

namespace test {

// Portable reference crypto for the host test build only (never compiled into the device
// image, which uses mbedTLS). SHA-256 per FIPS 180-4, HMAC per RFC 2104, PBKDF2 per RFC 8018;
// validated against published KAT vectors in test_main.cpp.
class RefCrypto : public runtime::Crypto {
public:
	void sha256(const uint8_t* data, size_t len, uint8_t out[32]) const override;
	void hmacSha256(const uint8_t* key, size_t keyLen,
	                const uint8_t* data, size_t dataLen, uint8_t out[32]) const override;
	void pbkdf2HmacSha256(const uint8_t* password, size_t passwordLen,
	                      const uint8_t* salt, size_t saltLen, uint32_t iterations,
	                      uint8_t* out, size_t outLen) const override;
};

}  // namespace test
