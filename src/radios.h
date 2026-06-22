#pragma once

#include "config_model.h"

void initRadios();
void sendRFCode(uint16_t freqMHz, const RFCode& code);

// RF receive (learn capture) per band (315/433); driven by src/learn.cpp. Both bands
// can arm at once: each radio's filter is deaf to the other, so exactly one captures a
// press. freqMHz selects the radio (unknown = no-op); the channel is freed on stop.
void startRFRx(uint16_t freqMHz);
void stopRFRx(uint16_t freqMHz);
// Collapse one captured OOK frame into a us mark/space burst; false if none ready. Re-arms RX.
bool pollRFBurst(uint16_t freqMHz, uint16_t* out, size_t cap, size_t* outLen);
