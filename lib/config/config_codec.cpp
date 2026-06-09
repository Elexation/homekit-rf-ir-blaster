#include "config_codec.h"

#include <string>
#include <utility>

#include "config_json.h"
#include "config_validate.h"
#include "crc32.h"

namespace config {

static void putU16(std::vector<uint8_t>& b, uint16_t v) {
	b.push_back(static_cast<uint8_t>(v & 0xFF));
	b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

static void putU32(std::vector<uint8_t>& b, uint32_t v) {
	b.push_back(static_cast<uint8_t>(v & 0xFF));
	b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
	b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static uint16_t getU16(const uint8_t* p) {
	return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t getU32(const uint8_t* p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool encode(const Config& cfg, std::vector<uint8_t>& out) {
	std::string payload;
	if (!toJson(cfg, payload)) return false;  // over the byte ceiling

	const uint8_t* bytes = reinterpret_cast<const uint8_t*>(payload.data());
	const uint32_t crc = crc32(bytes, payload.size());

	out.clear();
	out.reserve(BLOB_HEADER_SIZE + payload.size());
	putU32(out, BLOB_MAGIC);
	putU16(out, cfg.schemaVersion);
	putU32(out, static_cast<uint32_t>(payload.size()));
	putU32(out, crc);
	out.insert(out.end(), bytes, bytes + payload.size());
	return true;
}

DecodeResult decode(const std::vector<uint8_t>& blob) {
	DecodeResult r;  // defaults, Absent, usedDefaults = true

	if (blob.empty()) {
		r.status = DecodeStatus::Absent;
		return r;
	}
	if (blob.size() < BLOB_HEADER_SIZE) {
		r.status = DecodeStatus::BadHeader;
		return r;
	}

	const uint8_t* p = blob.data();
	if (getU32(p) != BLOB_MAGIC) {
		r.status = DecodeStatus::BadMagic;
		return r;
	}
	if (getU16(p + 4) != SCHEMA_VERSION) {
		r.status = DecodeStatus::BadVersion;
		return r;
	}
	const uint32_t len = getU32(p + 6);
	if (len > MAX_CONFIG_BYTES || BLOB_HEADER_SIZE + len != blob.size()) {
		r.status = DecodeStatus::BadHeader;
		return r;
	}
	const uint32_t crc = getU32(p + 10);
	const uint8_t* payload = p + BLOB_HEADER_SIZE;
	if (crc32(payload, len) != crc) {
		r.status = DecodeStatus::BadCrc;
		return r;
	}

	Config parsed;
	if (!fromJson(reinterpret_cast<const char*>(payload), len, parsed)) {
		r.status = DecodeStatus::BadJson;
		return r;
	}
	if (!isValid(parsed)) {
		r.status = DecodeStatus::FailedValidation;
		return r;
	}

	r.config = std::move(parsed);
	r.status = DecodeStatus::Ok;
	r.usedDefaults = false;
	return r;
}

bool save(IBlobStore& store, const Config& cfg) {
	std::vector<uint8_t> blob;
	if (!encode(cfg, blob)) return false;
	return store.put(blob.data(), blob.size());
}

DecodeResult load(IBlobStore& store) {
	std::vector<uint8_t> blob;
	if (!store.get(blob)) return DecodeResult{};  // Absent -> defaults
	return decode(blob);
}

}  // namespace config
