#include "password.h"

#include <string>
#include <vector>

#include "auth_config.h"
#include "encoding.h"

namespace runtime {

static const char kPrefix[] = "pbkdf2_sha256";

std::string deriveCredential(const Crypto& crypto, const std::string& password,
                             const uint8_t* salt, size_t saltLen, uint32_t iterations) {
	uint8_t dk[kDerivedKeyLen];
	crypto.pbkdf2HmacSha256(reinterpret_cast<const uint8_t*>(password.data()), password.size(),
	                        salt, saltLen, iterations, dk, kDerivedKeyLen);

	std::string rec = kPrefix;
	rec += '$';
	rec += std::to_string(iterations);
	rec += '$';
	rec += base64urlEncode(salt, saltLen);
	rec += '$';
	rec += base64urlEncode(dk, kDerivedKeyLen);
	return rec;
}

// Strict decimal -> uint32: empty, non-digit, overflow, and zero all rejected.
static bool parseIterations(const std::string& s, uint32_t& out) {
	if (s.empty() || s.size() > 10)
		return false;
	uint64_t v = 0;
	for (char c : s) {
		if (c < '0' || c > '9')
			return false;
		v = v * 10 + uint64_t(c - '0');
		if (v > 0xFFFFFFFFull)
			return false;
	}
	if (v == 0)
		return false;
	out = static_cast<uint32_t>(v);
	return true;
}

bool verifyPassword(const Crypto& crypto, const std::string& password, const std::string& record) {
	// Split into exactly 4 '$'-delimited fields; base64url and the tag never contain '$'.
	std::string fields[4];
	int fi = 0;
	for (char c : record) {
		if (c == '$') {
			if (++fi > 3)
				return false;
		} else {
			fields[fi].push_back(c);
		}
	}
	if (fi != 3 || fields[0] != kPrefix)
		return false;

	uint32_t iterations;
	if (!parseIterations(fields[1], iterations))
		return false;

	std::vector<uint8_t> salt, dk;
	if (!base64urlDecode(fields[2], salt) || salt.empty())
		return false;
	if (!base64urlDecode(fields[3], dk) || dk.size() != kDerivedKeyLen)
		return false;

	uint8_t cand[kDerivedKeyLen];
	crypto.pbkdf2HmacSha256(reinterpret_cast<const uint8_t*>(password.data()), password.size(),
	                        salt.data(), salt.size(), iterations, cand, kDerivedKeyLen);
	return constantTimeEqual(cand, dk.data(), kDerivedKeyLen);
}

}  // namespace runtime
