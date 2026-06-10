#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace runtime {

// base64url (RFC 4648 alphabet with '-' and '_', no '=' padding) for record/token fields.
std::string base64urlEncode(const uint8_t* data, size_t len);
std::string base64urlEncode(const std::string& data);

// false on any non-alphabet character or an impossible length (one leftover sextet).
bool base64urlDecode(const std::string& in, std::vector<uint8_t>& out);

}  // namespace runtime
