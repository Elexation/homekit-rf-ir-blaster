#pragma once

#include <cstddef>
#include <cstdint>

namespace config {

// CRC-32/ISO-HDLC over the persisted payload; guards against flash corruption.
uint32_t crc32(const uint8_t* data, size_t len);

}  // namespace config
