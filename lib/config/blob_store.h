#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace config {

// The single on-flash config blob; memory-backed on host, NVS/LittleFS on device.
class IBlobStore {
public:
	virtual ~IBlobStore() = default;

	// Reads the blob into out; false if absent or read failed (out untouched).
	virtual bool get(std::vector<uint8_t>& out) = 0;

	// Writes the blob; false if not fully persisted, leaving the prior blob intact.
	virtual bool put(const uint8_t* data, size_t len) = 0;

	// Removes the stored blob.
	virtual void erase() = 0;
};

}  // namespace config
