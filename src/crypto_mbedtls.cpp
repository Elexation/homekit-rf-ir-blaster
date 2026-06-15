#include "crypto_mbedtls.h"

#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>

namespace runtime {

void CryptoMbedTls::sha256(const uint8_t* data, size_t len, uint8_t out[32]) const {
	mbedtls_sha256(data, len, out, 0);  // 0 -> SHA-256, not SHA-224
}

void CryptoMbedTls::hmacSha256(const uint8_t* key, size_t keyLen,
                               const uint8_t* data, size_t dataLen, uint8_t out[32]) const {
	mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
	                key, keyLen, data, dataLen, out);
}

void CryptoMbedTls::pbkdf2HmacSha256(const uint8_t* password, size_t passwordLen,
                                     const uint8_t* salt, size_t saltLen, uint32_t iterations,
                                     uint8_t* out, size_t outLen) const {
	mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, password, passwordLen,
	                              salt, saltLen, iterations,
	                              static_cast<uint32_t>(outLen), out);
}

}  // namespace runtime
