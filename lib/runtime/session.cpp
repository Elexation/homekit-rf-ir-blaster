#include "session.h"

#include <string>
#include <vector>

#include "auth_config.h"
#include "encoding.h"

namespace runtime {

static std::string mintRaw(const Crypto& crypto, const std::string& secret, const std::string& user,
                           uint64_t iat, uint64_t absExpiry, uint64_t idleDeadline) {
	std::string payload = user;
	payload += '|';
	payload += std::to_string(iat);
	payload += '|';
	payload += std::to_string(absExpiry);
	payload += '|';
	payload += std::to_string(idleDeadline);

	std::string part1 = base64urlEncode(payload);
	uint8_t mac[32];
	crypto.hmacSha256(reinterpret_cast<const uint8_t*>(secret.data()), secret.size(),
	                  reinterpret_cast<const uint8_t*>(part1.data()), part1.size(), mac);
	return part1 + "." + base64urlEncode(mac, 32);
}

struct ParsedToken {
	std::string user;
	uint64_t    iat = 0;
	uint64_t    absExpiry = 0;
	uint64_t    idleDeadline = 0;
};

static bool parseUint64(const std::string& s, uint64_t& out) {
	if (s.empty() || s.size() > 20)
		return false;
	uint64_t v = 0;
	for (char c : s) {
		if (c < '0' || c > '9')
			return false;
		uint64_t nv = v * 10 + uint64_t(c - '0');
		if (nv < v)  // overflow
			return false;
		v = nv;
	}
	out = v;
	return true;
}

// MAC-checks the encoded payload before decoding any field: never parse unauthenticated bytes.
static bool parseToken(const Crypto& crypto, const std::string& secret, const std::string& token,
                       ParsedToken& out) {
	size_t dot = token.find('.');
	if (dot == std::string::npos || token.find('.', dot + 1) != std::string::npos)
		return false;
	std::string part1 = token.substr(0, dot);
	std::string part2 = token.substr(dot + 1);

	uint8_t mac[32];
	crypto.hmacSha256(reinterpret_cast<const uint8_t*>(secret.data()), secret.size(),
	                  reinterpret_cast<const uint8_t*>(part1.data()), part1.size(), mac);
	std::vector<uint8_t> sig;
	if (!base64urlDecode(part2, sig) || sig.size() != 32)
		return false;
	if (!constantTimeEqual(mac, sig.data(), 32))
		return false;

	std::vector<uint8_t> raw;
	if (!base64urlDecode(part1, raw))
		return false;
	std::string payload(raw.begin(), raw.end());

	std::string f[4];
	int fi = 0;
	for (char c : payload) {
		if (c == '|') {
			if (++fi > 3)
				return false;
		} else {
			f[fi].push_back(c);
		}
	}
	if (fi != 3 || f[0].empty())
		return false;

	out.user = f[0];
	if (!parseUint64(f[1], out.iat) || !parseUint64(f[2], out.absExpiry) ||
	    !parseUint64(f[3], out.idleDeadline))
		return false;
	return true;
}

std::string mintSession(const Crypto& crypto, const std::string& secret, const std::string& user,
                        uint64_t iat) {
	if (user.empty() || user.find('|') != std::string::npos)
		return "";
	return mintRaw(crypto, secret, user, iat, iat + kAbsoluteTimeoutMs, iat + kIdleTimeoutMs);
}

SessionResult verifySession(const Crypto& crypto, const std::string& secret,
                            const std::string& token, uint64_t now) {
	SessionResult r;
	ParsedToken p;
	if (!parseToken(crypto, secret, token, p))
		return r;
	r.user = p.user;
	r.binding = p.user + "|" + std::to_string(p.iat);
	r.expiredIdle = now >= p.idleDeadline;
	r.expiredAbsolute = now >= p.absExpiry;
	r.valid = !r.expiredIdle && !r.expiredAbsolute;
	return r;
}

std::string refreshSession(const Crypto& crypto, const std::string& secret,
                           const std::string& token, uint64_t now) {
	ParsedToken p;
	if (!parseToken(crypto, secret, token, p))
		return "";
	if (now >= p.absExpiry || now >= p.idleDeadline)
		return "";
	return mintRaw(crypto, secret, p.user, p.iat, p.absExpiry, now + kIdleTimeoutMs);
}

}  // namespace runtime
