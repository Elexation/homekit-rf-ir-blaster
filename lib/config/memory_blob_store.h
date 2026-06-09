#pragma once

#include "blob_store.h"

namespace config {

// In-memory IBlobStore for host tests; the capacity ceiling drives the store-full path.
class MemoryBlobStore : public IBlobStore {
public:
	static constexpr size_t kUnlimited = static_cast<size_t>(-1);

	explicit MemoryBlobStore(size_t capacity = kUnlimited) : capacity_(capacity) {}

	bool get(std::vector<uint8_t>& out) override {
		if (!present_) return false;
		out = blob_;
		return true;
	}

	bool put(const uint8_t* data, size_t len) override {
		if (len > capacity_) return false;  // full: keep the existing blob
		blob_.assign(data, data + len);
		present_ = true;
		return true;
	}

	void erase() override {
		blob_.clear();
		present_ = false;
	}

	bool present() const { return present_; }
	size_t size() const { return blob_.size(); }

private:
	size_t               capacity_;
	bool                 present_ = false;
	std::vector<uint8_t> blob_;
};

}  // namespace config
