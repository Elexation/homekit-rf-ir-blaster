#include "request_policy.h"

namespace runtime {

static char lower(char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Host header without any ":port" suffix.
static std::string hostOnly(const std::string& h) {
	size_t colon = h.find(':');
	return colon == std::string::npos ? h : h.substr(0, colon);
}

// Case-insensitive host compare, ignoring one optional trailing dot on either side.
static bool hostEquals(const std::string& a, const std::string& b) {
	size_t la = a.size(), lb = b.size();
	if (la && a[la - 1] == '.') --la;
	if (lb && b[lb - 1] == '.') --lb;
	if (la != lb)
		return false;
	for (size_t i = 0; i < la; ++i)
		if (lower(a[i]) != lower(b[i]))
			return false;
	return true;
}

// Trust X-Forwarded-Proto only behind a trusted proxy; it is client-spoofable otherwise.
static Scheme effectiveScheme(const Request& req, bool trustedProxy) {
	if (!trustedProxy || req.forwardedProto.empty())
		return req.transportScheme;
	const std::string& p = req.forwardedProto;
	size_t comma = p.find(',');
	std::string tok = (comma == std::string::npos) ? p : p.substr(0, comma);
	size_t b = tok.find_first_not_of(" \t");
	if (b == std::string::npos)
		return req.transportScheme;
	size_t e = tok.find_last_not_of(" \t");
	tok = tok.substr(b, e - b + 1);
	bool isHttps = tok.size() == 5 && lower(tok[0]) == 'h' && lower(tok[1]) == 't' &&
	               lower(tok[2]) == 't' && lower(tok[3]) == 'p' && lower(tok[4]) == 's';
	return isHttps ? Scheme::Https : Scheme::Http;
}

Decision evaluate(const Request& req, const config::Settings& settings) {
	if (req.isLoopback)
		return { Action::Serve, {} };

	const Scheme eff = effectiveScheme(req, settings.trustedProxy);
	const std::string host = hostOnly(req.host);
	const bool canonicalActive = settings.https || settings.trustedProxy;

	// HTTPS on means plaintext is never served: always redirect it to HTTPS.
	if (settings.https && eff == Scheme::Http) {
		const std::string& h =
		    settings.canonicalDomain.empty() ? host : settings.canonicalDomain;
		return { Action::Redirect, "https://" + h + req.target };
	}

	// Pin a mismatched Host to the canonical host once the scheme is acceptable.
	if (canonicalActive && !settings.canonicalDomain.empty() &&
	    !hostEquals(host, settings.canonicalDomain)) {
		const char* scheme = (eff == Scheme::Https) ? "https://" : "http://";
		return { Action::Redirect, scheme + settings.canonicalDomain + req.target };
	}

	return { Action::Serve, {} };
}

}  // namespace runtime
