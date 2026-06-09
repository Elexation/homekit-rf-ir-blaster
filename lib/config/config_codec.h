#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "blob_store.h"
#include "config_model.h"

namespace config {

// Marks our config blob; a mismatch loads defaults. ASCII "RFB1".
constexpr uint32_t BLOB_MAGIC = 0x52464231u;

constexpr size_t BLOB_HEADER_SIZE = 4 + 2 + 4 + 4;  // magic, version, len, crc32

enum class DecodeStatus : uint8_t {
	Ok = 0,
	Absent,            // nothing stored
	BadHeader,         // too short, or declared length disagrees with the blob
	BadMagic,
	BadVersion,
	BadCrc,
	BadJson,
	FailedValidation,
};

struct DecodeResult {
	Config       config;                       // defaults whenever usedDefaults
	DecodeStatus status       = DecodeStatus::Absent;
	bool         usedDefaults = true;
};

// Serialize cfg to [magic][version][len][crc32] + JSON payload; false if over the byte ceiling.
bool encode(const Config& cfg, std::vector<uint8_t>& out);

// Parse an envelope; any corruption yields defaults instead of failing hard.
DecodeResult decode(const std::vector<uint8_t>& blob);

// Blob-store helpers; save() returns false if the store rejected the write (prior blob kept).
bool save(IBlobStore& store, const Config& cfg);
DecodeResult load(IBlobStore& store);

}  // namespace config
