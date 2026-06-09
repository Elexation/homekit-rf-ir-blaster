#pragma once

#include <nvs.h>

#include "blob_store.h"

namespace config {

// IBlobStore backed by a dedicated ESP32 NVS namespace, whole config as one blob.
class NvsBlobStore : public IBlobStore {
public:
	NvsBlobStore();
	~NvsBlobStore() override;

	bool get(std::vector<uint8_t>& out) override;
	bool put(const uint8_t* data, size_t len) override;
	void erase() override;

private:
	nvs_handle_t handle_ = 0;
	bool         ok_      = false;  // namespace opened
};

}  // namespace config
