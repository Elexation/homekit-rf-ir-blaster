#pragma once

#include <cstdint>

struct RFCode {
	const uint16_t* pulses;   // high/low duration pairs, microseconds
	uint16_t        length;   // entry count, must be even
	uint16_t        freqMHz;  // 315 or 433
};

struct IRCode {
	const uint16_t* pulses;
	uint16_t        length;
	uint16_t        carrierHz;  // measured at capture time
};

void loadCodes();

// Placeholder code data, populated once real codes are captured on-device.
constexpr uint16_t screen_up_pulses[]    = { 0, 0 };
constexpr uint16_t screen_stop_pulses[]  = { 0, 0 };
constexpr uint16_t screen_down_pulses[]  = { 0, 0 };
constexpr uint16_t power_toggle_pulses[] = { 0, 0 };

constexpr RFCode SCREEN_UP    = { screen_up_pulses,    sizeof(screen_up_pulses) / 2,    315 };
constexpr RFCode SCREEN_STOP  = { screen_stop_pulses,  sizeof(screen_stop_pulses) / 2,  315 };
constexpr RFCode SCREEN_DOWN  = { screen_down_pulses,  sizeof(screen_down_pulses) / 2,  315 };
constexpr IRCode POWER_TOGGLE = { power_toggle_pulses, sizeof(power_toggle_pulses) / 2, 38000 };
