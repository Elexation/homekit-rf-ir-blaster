#pragma once

enum class UiState {
	Normal,
	SafeModeArmed,  // BOOT held 5 s: release to enter safe mode
	FactoryArmed,   // BOOT held 10 s: release to factory reset
	SafeMode,
};

void initUI();
void updateUI();
void setUiState(UiState s);
void setLedEnabled(bool enabled);  // off darkens the LED only when healthy; recovery/setup still show
