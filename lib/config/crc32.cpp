#include "crc32.h"

namespace config {

uint32_t crc32(const uint8_t* data, size_t len) {
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; ++i) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; ++bit) {
			crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
		}
	}
	return crc ^ 0xFFFFFFFFu;
}

}  // namespace config
