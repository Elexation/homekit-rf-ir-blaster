#pragma once

#include <cstddef>
#include <string>

namespace runtime {

// Build a Set-Cookie value. secure=true uses the __Host- prefix + Secure (RFC 6265bis:
// requires Secure + Path=/ + no Domain); secure=false drops both for plain HTTP, keeping
// HttpOnly + SameSite=Strict + Path=/. The prefix and Secure always move together.
std::string sessionCookie(const std::string& token, bool secure);
std::string clearSessionCookie(bool secure);
std::string csrfCookie(const std::string& token, bool secure);

// Cookie names matching the flavor above; the read side selects by the serving scheme.
const char* sessionCookieName(bool secure);
const char* csrfCookieName(bool secure);

// Strict-Transport-Security value; the device sends it only when https.
std::string hstsHeader();

// Static response headers for every config-panel response.
struct SecurityHeader {
	const char* name;
	const char* value;
};
extern const SecurityHeader kStaticSecurityHeaders[];
extern const size_t         kStaticSecurityHeaderCount;

}  // namespace runtime
