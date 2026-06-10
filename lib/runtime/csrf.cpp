#include "csrf.h"

#include <vector>

#include "encoding.h"

namespace runtime {

static void csrfMac(const Crypto& crypto, const std::string& secret, const std::string& binding,
                    const uint8_t* rand, size_t randLen, uint8_t out[32]) {
	std::string msg = binding;
	msg += '!';
	msg.append(reinterpret_cast<const char*>(rand), randLen);
	crypto.hmacSha256(reinterpret_cast<const uint8_t*>(secret.data()), secret.size(),
	                  reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), out);
}

std::string mintCsrf(const Crypto& crypto, const std::string& secret, const std::string& binding,
                     const uint8_t* rand, size_t randLen) {
	uint8_t mac[32];
	csrfMac(crypto, secret, binding, rand, randLen, mac);
	return base64urlEncode(rand, randLen) + "." + base64urlEncode(mac, 32);
}

bool verifyCsrf(const Crypto& crypto, const std::string& secret, const std::string& binding,
                const std::string& cookieTok, const std::string& formTok) {
	// Double-submit: the cookie and the form field must carry the identical token.
	if (cookieTok.empty() || !constantTimeEqual(cookieTok, formTok))
		return false;

	size_t dot = cookieTok.find('.');
	if (dot == std::string::npos || cookieTok.find('.', dot + 1) != std::string::npos)
		return false;

	std::vector<uint8_t> rand;
	if (!base64urlDecode(cookieTok.substr(0, dot), rand))
		return false;
	std::vector<uint8_t> sig;
	if (!base64urlDecode(cookieTok.substr(dot + 1), sig) || sig.size() != 32)
		return false;

	uint8_t mac[32];
	csrfMac(crypto, secret, binding, rand.data(), rand.size(), mac);
	return constantTimeEqual(mac, sig.data(), 32);
}

}  // namespace runtime
