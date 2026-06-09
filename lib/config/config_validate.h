#pragma once

#include "config_model.h"

namespace config {

enum class ValidateError {
	Ok = 0,
	TooManyDevices,
	DuplicateDeviceId,
	NextIdNotMonotonic,
	BadService,
	EmptyLearnedCode,
	OddPulseCount,
	TooManyPulses,
	BadRfFreq,
	BadIrCarrier,
	BadListenPort,
};

// Structural bounds and invariants; byte-size and depth ceilings live in the codec.
ValidateError validate(const Config& cfg);

inline bool isValid(const Config& cfg) {
	return validate(cfg) == ValidateError::Ok;
}

}  // namespace config
