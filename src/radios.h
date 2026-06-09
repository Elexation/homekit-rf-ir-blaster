#pragma once

#include "codes.h"

void initRadios();
void sendRFCode(uint16_t freqMHz, const RFCode& code);
