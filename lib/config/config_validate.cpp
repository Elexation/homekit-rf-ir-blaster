#include "config_validate.h"

#include <set>

namespace config {

ValidateError validate(const Config& cfg) {
	if (cfg.devices.size() > MAX_DEVICES)
		return ValidateError::TooManyDevices;

	if (cfg.settings.listenPort == 0)
		return ValidateError::BadListenPort;

	std::set<uint16_t> seenIds;
	for (const auto& d : cfg.devices) {
		if (!seenIds.insert(d.id).second)
			return ValidateError::DuplicateDeviceId;
		if (d.id < 2)
			return ValidateError::ReservedDeviceId;  // 0/1 reserved; aid 1 is the bridge
		if (d.id >= cfg.nextDeviceId)
			return ValidateError::NextIdNotMonotonic;
		if (d.service.empty())
			return ValidateError::BadService;

		for (const auto& slot : d.commands) {
			const StoredCode& code = slot.code;
			if (code.kind == CodeKind::None)
				continue;  // unlearned slot; allowed
			if (code.pulses.empty())
				return ValidateError::EmptyLearnedCode;
			if (code.pulses.size() % 2 != 0)
				return ValidateError::OddPulseCount;
			if (code.pulses.size() > MAX_PULSES)
				return ValidateError::TooManyPulses;
			if (code.kind == CodeKind::RF) {
				if (code.freqMHz != 315 && code.freqMHz != 433)
					return ValidateError::BadRfFreq;
			} else if (code.kind == CodeKind::IR) {
				if (code.carrierHz < 20000 || code.carrierHz > 60000)
					return ValidateError::BadIrCarrier;
			}
		}
	}
	return ValidateError::Ok;
}

}  // namespace config
