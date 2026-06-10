#include "client_key.h"

#include "ip_util.h"

namespace runtime {

// First comma-separated token, trimmed; X-Forwarded-For's first hop is the original client.
static std::string firstHop(const std::string& s) {
	size_t comma = s.find(',');
	std::string tok = (comma == std::string::npos) ? s : s.substr(0, comma);
	size_t b = tok.find_first_not_of(" \t");
	if (b == std::string::npos)
		return std::string();
	size_t e = tok.find_last_not_of(" \t");
	return tok.substr(b, e - b + 1);
}

uint32_t deriveClientKey(uint32_t peerIpv4, bool trustedProxy,
                         const std::string& xRealIp, const std::string& xForwardedFor) {
	if (trustedProxy) {
		const std::string candidate = !xRealIp.empty() ? firstHop(xRealIp) : firstHop(xForwardedFor);
		uint32_t ip;
		if (config::parseIPv4(candidate, ip))
			return ip;
	}
	return peerIpv4;
}

}  // namespace runtime
