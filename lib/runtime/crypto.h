#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace runtime {

// Crypto seam: the device binds mbedTLS (possibly HW SHA), the host test build a reference
// impl, so the auth logic (records, tokens, expiry, compares) is host-tested regardless of
// which backend computes the primitives.
class Crypto {
public:
	virtual ~Crypto() = default;

	virtual void sha256(const uint8_t* data, size_t len, uint8_t out[32]) const = 0;

	virtual void hmacSha256(const uint8_t* key, size_t keyLen,
	                        const uint8_t* data, size_t dataLen, uint8_t out[32]) const = 0;

	// Device path is mbedtls_pkcs5_pbkdf2_hmac_ext.
	virtual void pbkdf2HmacSha256(const uint8_t* password, size_t passwordLen,
	                              const uint8_t* salt, size_t saltLen, uint32_t iterations,
	                              uint8_t* out, size_t outLen) const = 0;
};

// Length-constant compare for MACs/secrets (no data-dependent early exit). Length is not
// treated as secret: a size mismatch returns false immediately.
inline bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t n) {
	uint8_t diff = 0;
	for (size_t i = 0; i < n; ++i)
		diff |= static_cast<uint8_t>(a[i] ^ b[i]);
	return diff == 0;
}

inline bool constantTimeEqual(const std::string& a, const std::string& b) {
	if (a.size() != b.size())
		return false;
	return constantTimeEqual(reinterpret_cast<const uint8_t*>(a.data()),
	                         reinterpret_cast<const uint8_t*>(b.data()), a.size());
}

}  // namespace runtime
