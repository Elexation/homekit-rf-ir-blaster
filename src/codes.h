#pragma once

// RFCode/IRCode come from the config layer.
#include "config_model.h"

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
