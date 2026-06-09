#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Lean transmit views; borrow a StoredCode's pulse buffer (no copy on send).
struct RFCode {
	const uint16_t* pulses;   // high/low durations in microseconds, paired
	uint16_t        length;   // entry count (even)
	uint16_t        freqMHz;  // 315 or 433
};

struct IRCode {
	const uint16_t* pulses;
	uint16_t        length;
	uint16_t        carrierHz;  // measured at capture time
};

namespace config {

// Bumped only on an incompatible persisted/exported layout change.
constexpr uint16_t SCHEMA_VERSION = 1;

// Load/import bounds so a malformed or oversized blob can't exhaust memory.
constexpr size_t  MAX_CONFIG_BYTES = 16384;  // serialized JSON payload ceiling
constexpr size_t  MAX_DEVICES      = 32;
constexpr size_t  MAX_PULSES       = 512;    // uint16_t entries per code
constexpr uint8_t NESTING_LIMIT    = 6;      // JSON object/array depth ceiling

enum class CodeKind : uint8_t {
	None = 0,  // nothing learned yet; never transmitted
	RF   = 1,
	IR   = 2,
};

// Owns its pulse buffer; asRFCode()/asIRCode() return views into it.
struct StoredCode {
	CodeKind              kind      = CodeKind::None;
	std::vector<uint16_t> pulses;
	uint16_t              freqMHz   = 0;      // RF only (315/433)
	uint16_t              carrierHz = 0;      // IR only (e.g. 38000)
	bool                  rolling   = false;  // RF only; rejected at learn, never stored

	bool isLearned() const {
		return kind != CodeKind::None && !pulses.empty();
	}

	RFCode asRFCode() const {
		return RFCode{ pulses.data(), static_cast<uint16_t>(pulses.size()), freqMHz };
	}

	IRCode asIRCode() const {
		return IRCode{ pulses.data(), static_cast<uint16_t>(pulses.size()), carrierHz };
	}
};

// One named command on a device ("up", "down", "stop", "on", "off", ...).
struct CommandSlot {
	std::string name;
	StoredCode  code;
};

struct DeviceOptions {
	uint8_t repeatCount = 1;  // times to re-send on each press
};

// One Apple Home tile; id is allocated once and never reused after deletion.
struct VirtualDevice {
	uint16_t                 id = 0;
	std::string              service;  // HomeKit service, e.g. "WindowCovering"
	std::string              name;     // display name
	DeviceOptions            options;
	std::vector<CommandSlot> commands;
};

// Web/config settings; no secrets here (password hash and setup code persist separately).
struct Settings {
	bool        https               = true;
	uint16_t    listenPort          = 443;
	bool        trustedProxy        = false;
	std::string canonicalDomain     = "blaster.local";
	bool        httpToHttpsRedirect = true;
	bool        requireHttps        = true;
};

struct Config {
	uint16_t                   schemaVersion = SCHEMA_VERSION;
	Settings                   settings;
	uint16_t                   nextDeviceId  = 2;  // id 1 is the bridge accessory
	std::vector<VirtualDevice> devices;
};

}  // namespace config
