#include "security_headers.h"

#include "auth_config.h"

namespace runtime {

static const char kSecureAttrs[] = "; Secure; HttpOnly; SameSite=Strict; Path=/";
static const char kPlainAttrs[]  = "; HttpOnly; SameSite=Strict; Path=/";

const char* sessionCookieName(bool secure) {
	return secure ? kSessionCookieName : kSessionCookieNamePlain;
}

const char* csrfCookieName(bool secure) {
	return secure ? kCsrfCookieName : kCsrfCookieNamePlain;
}

static const char* cookieAttrs(bool secure) {
	return secure ? kSecureAttrs : kPlainAttrs;
}

std::string sessionCookie(const std::string& token, bool secure) {
	return std::string(sessionCookieName(secure)) + "=" + token + cookieAttrs(secure);
}

std::string clearSessionCookie(bool secure) {
	return std::string(sessionCookieName(secure)) + "=" + cookieAttrs(secure) + "; Max-Age=0";
}

std::string csrfCookie(const std::string& token, bool secure) {
	return std::string(csrfCookieName(secure)) + "=" + token + cookieAttrs(secure);
}

std::string hstsHeader() {
	return "max-age=31536000";
}

const SecurityHeader kStaticSecurityHeaders[] = {
	{"X-Content-Type-Options", "nosniff"},
	{"X-Frame-Options", "DENY"},
	{"Referrer-Policy", "no-referrer"},
	{"Cache-Control", "no-store"},
	// default-src 'self' bans inline script/style; the UI is authored to satisfy it
	{"Content-Security-Policy",
	 "default-src 'self'; frame-ancestors 'none'; form-action 'self'; "
	 "base-uri 'none'; object-src 'none'"},
};
const size_t kStaticSecurityHeaderCount =
	sizeof(kStaticSecurityHeaders) / sizeof(kStaticSecurityHeaders[0]);

}  // namespace runtime
