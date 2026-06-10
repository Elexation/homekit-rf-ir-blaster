#include "encoding.h"

namespace runtime {

static const char kEnc[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64urlEncode(const uint8_t* data, size_t len) {
	std::string out;
	out.reserve((len + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 3 <= len; i += 3) {
		uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
		out.push_back(kEnc[(n >> 18) & 0x3F]);
		out.push_back(kEnc[(n >> 12) & 0x3F]);
		out.push_back(kEnc[(n >> 6) & 0x3F]);
		out.push_back(kEnc[n & 0x3F]);
	}
	size_t rem = len - i;
	if (rem == 1) {
		uint32_t n = uint32_t(data[i]) << 16;
		out.push_back(kEnc[(n >> 18) & 0x3F]);
		out.push_back(kEnc[(n >> 12) & 0x3F]);
	} else if (rem == 2) {
		uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
		out.push_back(kEnc[(n >> 18) & 0x3F]);
		out.push_back(kEnc[(n >> 12) & 0x3F]);
		out.push_back(kEnc[(n >> 6) & 0x3F]);
	}
	return out;
}

std::string base64urlEncode(const std::string& data) {
	return base64urlEncode(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

static int dec(char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '-') return 62;
	if (c == '_') return 63;
	return -1;
}

bool base64urlDecode(const std::string& in, std::vector<uint8_t>& out) {
	out.clear();
	size_t n = in.size();
	if (n % 4 == 1)
		return false;
	out.reserve(n / 4 * 3 + 2);
	size_t i = 0;
	for (; i + 4 <= n; i += 4) {
		int a = dec(in[i]), b = dec(in[i + 1]), c = dec(in[i + 2]), d = dec(in[i + 3]);
		if ((a | b | c | d) < 0)
			return false;
		uint32_t v = (uint32_t(a) << 18) | (uint32_t(b) << 12) | (uint32_t(c) << 6) | d;
		out.push_back(uint8_t(v >> 16));
		out.push_back(uint8_t(v >> 8));
		out.push_back(uint8_t(v));
	}
	size_t rem = n - i;
	if (rem == 2) {
		int a = dec(in[i]), b = dec(in[i + 1]);
		if ((a | b) < 0)
			return false;
		out.push_back(uint8_t((uint32_t(a) << 2) | (uint32_t(b) >> 4)));
	} else if (rem == 3) {
		int a = dec(in[i]), b = dec(in[i + 1]), c = dec(in[i + 2]);
		if ((a | b | c) < 0)
			return false;
		uint32_t v = (uint32_t(a) << 12) | (uint32_t(b) << 6) | c;
		out.push_back(uint8_t(v >> 10));
		out.push_back(uint8_t(v >> 2));
	}
	return true;
}

}  // namespace runtime
