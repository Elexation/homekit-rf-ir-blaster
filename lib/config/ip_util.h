#pragma once

#include <cstdint>
#include <string>

namespace config {

// Strict dotted-quad parse: four octets 0..255, no trailing dot. On success returns true
// and writes the address in host byte order (a.b.c.d -> (a<<24)|(b<<16)|(c<<8)|d).
inline bool parseIPv4(const std::string& s, uint32_t& out) {
	uint32_t acc = 0;
	int parts = 0;
	size_t i = 0, n = s.size();
	while (i < n) {
		int val = 0, digits = 0;
		while (i < n && s[i] >= '0' && s[i] <= '9') {
			val = val * 10 + (s[i] - '0');
			if (++digits > 3)
				return false;
			++i;
		}
		if (digits == 0 || val > 255)
			return false;
		acc = (acc << 8) | static_cast<uint32_t>(val);
		++parts;
		if (i < n) {
			if (s[i] != '.')
				return false;
			if (++i == n)
				return false;  // no trailing dot
		}
	}
	if (parts != 4)
		return false;
	out = acc;
	return true;
}

inline bool isIPv4(const std::string& s) {
	uint32_t tmp;
	return parseIPv4(s, tmp);
}

}  // namespace config
