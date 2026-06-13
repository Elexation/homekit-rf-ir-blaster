#include "config_mutate.h"

#include <utility>

#include "accessory_plan.h"
#include "config_codec.h"
#include "config_validate.h"

namespace config {

static bool badName(const std::string& s) {
	return s.empty() || s.size() > MAX_NAME_LEN;
}

static MutateError commit(IBlobStore& store, Config& cfg, Config&& next) {
	if (validate(next) != ValidateError::Ok)
		return MutateError::FailedValidation;
	if (!save(store, next))
		return MutateError::StoreRejected;
	cfg = std::move(next);
	return MutateError::Ok;
}

MutateError upsertCommand(IBlobStore& store, Config& cfg, uint16_t deviceId,
                          const std::string& commandName, const StoredCode& code) {
	if (badName(commandName))
		return MutateError::BadName;
	Config next = cfg;
	VirtualDevice* dev = nullptr;
	for (auto& d : next.devices) {
		if (d.id == deviceId) {
			dev = &d;
			break;
		}
	}
	if (dev == nullptr)
		return MutateError::DeviceNotFound;
	CommandSlot* slot = nullptr;
	for (auto& s : dev->commands) {
		if (s.name == commandName) {
			slot = &s;
			break;
		}
	}
	if (slot != nullptr)
		slot->code = code;
	else
		dev->commands.push_back({ commandName, code });
	return commit(store, cfg, std::move(next));
}

AddDeviceResult addDevice(IBlobStore& store, Config& cfg, const std::string& deviceName,
                          const std::string& service, const std::string& commandName,
                          const StoredCode& code) {
	AddDeviceResult res;
	if (badName(deviceName) || badName(commandName)) {
		res.error = MutateError::BadName;
		return res;
	}
	if (serviceTypeFromString(service) == ServiceType::Unknown) {
		res.error = MutateError::BadService;
		return res;
	}
	if (cfg.devices.size() >= MAX_DEVICES) {
		res.error = MutateError::DeviceLimit;
		return res;
	}
	Config next = cfg;
	VirtualDevice dev;
	dev.id = next.nextDeviceId;
	dev.service = service;
	dev.name = deviceName;
	dev.commands.push_back({ commandName, code });
	next.nextDeviceId = static_cast<uint16_t>(next.nextDeviceId + 1);
	next.devices.push_back(std::move(dev));
	res.error = commit(store, cfg, std::move(next));
	if (res.error == MutateError::Ok)
		res.id = cfg.devices.back().id;
	return res;
}

}  // namespace config
