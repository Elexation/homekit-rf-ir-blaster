#pragma once

#include <cstdint>
#include <string>

#include "blob_store.h"
#include "config_model.h"

namespace config {

// Device/command name ceiling; mirrors the web UI input limit.
constexpr size_t MAX_NAME_LEN = 32;

enum class MutateError : uint8_t {
	Ok = 0,
	BadName,           // empty or over MAX_NAME_LEN
	BadService,        // not a supported HomeKit service type
	DeviceNotFound,
	DeviceLimit,
	FailedValidation,
	StoreRejected,     // encode or persist failed
};

struct AddDeviceResult {
	MutateError error = MutateError::Ok;
	uint16_t    id    = 0;  // new device id, set only when Ok
};

// Both helpers validate+persist a working copy, swapping into cfg only on
// success; any failure leaves cfg and the stored blob untouched.

// Adds or replaces a same-named command on an existing device.
MutateError upsertCommand(IBlobStore& store, Config& cfg, uint16_t deviceId,
                          const std::string& commandName, const StoredCode& code);

// Creates a device with one command; id from nextDeviceId (grows, never reused).
AddDeviceResult addDevice(IBlobStore& store, Config& cfg, const std::string& deviceName,
                          const std::string& service, const std::string& commandName,
                          const StoredCode& code);

}  // namespace config
