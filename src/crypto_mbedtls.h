#pragma once

#include "crypto.h"

namespace runtime {

// Device adapter for the Crypto seam (mbedTLS); keeps real crypto out of lib/runtime.
class CryptoMbedTls : public Crypto {
public:
	void sha256(const uint8_t* data, size_t len, uint8_t out[32]) const override;
	void hmacSha256(const uint8_t* key, size_t keyLen,
	                const uint8_t* data, size_t dataLen, uint8_t out[32]) const override;
	void pbkdf2HmacSha256(const uint8_t* password, size_t passwordLen,
	                      const uint8_t* salt, size_t saltLen, uint32_t iterations,
	                      uint8_t* out, size_t outLen) const override;
};

}  // namespace runtime
