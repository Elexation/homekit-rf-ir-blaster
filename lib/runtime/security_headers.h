#pragma once

#include <cstddef>
#include <string>

namespace runtime {

// Set-Cookie values; the device sets the "Set-Cookie" field name and these build the value.
// The __Host- prefix requires Secure + Path=/ + no Domain (RFC 6265bis).
std::string sessionCookie(const std::string& token);
std::string clearSessionCookie();
std::string csrfCookie(const std::string& token);

// Strict-Transport-Security value; the device sends it only when requireHttps.
std::string hstsHeader();

// Static response headers set on every config-panel response (the device sets each name/value).
struct SecurityHeader {
	const char* name;
	const char* value;
};
extern const SecurityHeader kStaticSecurityHeaders[];
extern const size_t         kStaticSecurityHeaderCount;

}  // namespace runtime
