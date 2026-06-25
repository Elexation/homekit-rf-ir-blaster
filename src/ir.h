#pragma once

#include "config_model.h"

void initIR();
bool sendIR(const IRCode& code);

// IR receive (learn capture); driven by src/learn.cpp.
void startIRRx();
void stopIRRx();
// Collapse one captured frame into a us mark/space burst + carrierHz; false if none ready. Re-arms RX.
bool pollIRBurst(uint16_t* out, size_t cap, size_t* outLen, uint16_t* outCarrierHz);
