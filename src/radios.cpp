#include "radios.h"

void initRadios() {
	// TODO: bring up both SPI buses and both CC1101 modules in async direct mode
}

void sendRFCode(uint16_t freqMHz, const RFCode& code) {
	// TODO: drive the band-matched radio via RMT, then return it to RX idle
	(void)freqMHz;
	(void)code;
}
