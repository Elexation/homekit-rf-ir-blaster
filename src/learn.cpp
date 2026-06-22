#include "learn.h"

#include <Arduino.h>

#include "ir.h"
#include "radios.h"

namespace {

learn::LearnMachine g_learn;
bool                g_active = false;  // a learn session is armed (IR + RF)
uint16_t            g_pulseBuf[config::MAX_PULSES];

}  // namespace

// One learn arms every band (IR + RF 315/433); each radio is deaf to the others, so one captures.
void startLearn() {
	g_learn.begin(millis());
	startIRRx();
	startRFRx(315);
	startRFRx(433);
	g_active = true;
}

void cancelLearn() {
	g_learn.cancel();
	stopIRRx();
	stopRFRx(315);
	stopRFRx(433);
	g_active = false;
}

void pollLearn() {
	if (!g_active)
		return;
	uint64_t now     = millis();
	size_t   len     = 0;
	uint16_t carrier = 0;
	while (pollIRBurst(g_pulseBuf, config::MAX_PULSES, &len, &carrier)) {
		g_learn.feedBurst(learn::Source::Ir, g_pulseBuf, len, carrier, now);
		if (g_learn.state() != learn::State::Listening)
			break;
	}
	while (g_learn.state() == learn::State::Listening &&
	       pollRFBurst(315, g_pulseBuf, config::MAX_PULSES, &len)) {
		g_learn.feedBurst(learn::Source::Rf315, g_pulseBuf, len, 0, now);
		if (g_learn.state() != learn::State::Listening)
			break;
	}
	while (g_learn.state() == learn::State::Listening &&
	       pollRFBurst(433, g_pulseBuf, config::MAX_PULSES, &len)) {
		g_learn.feedBurst(learn::Source::Rf433, g_pulseBuf, len, 0, now);
		if (g_learn.state() != learn::State::Listening)
			break;
	}
	if (g_learn.tick(now) != learn::State::Listening) {
		stopIRRx();
		stopRFRx(315);
		stopRFRx(433);
		g_active = false;
	}
}

learn::State learnState() {
	return g_learn.state();
}

learn::FailReason learnFailReason() {
	return g_learn.failReason();
}

config::StoredCode takeLearnedCode() {
	return g_learn.takeCode();
}
