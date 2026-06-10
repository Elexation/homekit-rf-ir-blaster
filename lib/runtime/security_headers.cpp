#include "security_headers.h"

#include "auth_config.h"

namespace runtime {

static const char kCookieAttrs[] = "; Secure; HttpOnly; SameSite=Strict; Path=/";

std::string sessionCookie(const std::string& token) {
	return std::string(kSessionCookieName) + "=" + token + kCookieAttrs;
}

std::string clearSessionCookie() {
	return std::string(kSessionCookieName) + "=" + kCookieAttrs + "; Max-Age=0";
}

std::string csrfCookie(const std::string& token) {
	return std::string(kCsrfCookieName) + "=" + token + kCookieAttrs;
}

std::string hstsHeader() {
	return "max-age=31536000";
}

const SecurityHeader kStaticSecurityHeaders[] = {
	{"X-Content-Type-Options", "nosniff"},
	{"X-Frame-Options", "DENY"},
	{"Referrer-Policy", "no-referrer"},
	{"Cache-Control", "no-store"},
};
const size_t kStaticSecurityHeaderCount =
	sizeof(kStaticSecurityHeaders) / sizeof(kStaticSecurityHeaders[0]);

}  // namespace runtime
