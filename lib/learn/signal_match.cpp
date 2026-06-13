#include "signal_match.h"

#include <algorithm>

namespace learn {

bool durationsMatch(uint16_t a, uint16_t b) {
	uint32_t diff = a > b ? static_cast<uint32_t>(a - b) : static_cast<uint32_t>(b - a);
	uint32_t ref = std::max<uint32_t>(a, b);
	return diff * 100u <= ref * kMatchTolerancePercent || diff <= kMatchToleranceUs;
}

bool wellFormed(Source src, const uint16_t* pulses, size_t len, uint16_t carrierHz) {
	if (len < kMinBurstPulses || len > kMaxBurstPulses)
		return false;
	for (size_t i = 0; i < len; ++i) {
		if (pulses[i] == 0)
			return false;
	}
	if (src == Source::Ir && (carrierHz < kIrCarrierMinHz || carrierHz > kIrCarrierMaxHz))
		return false;
	return true;
}

bool burstsMatch(const uint16_t* a, size_t lenA, const uint16_t* b, size_t lenB) {
	if (lenA != lenB)
		return false;
	for (size_t i = 0; i < lenA; ++i) {
		if (!durationsMatch(a[i], b[i]))
			return false;
	}
	return true;
}

bool structurallyAlike(const uint16_t* a, size_t lenA, const uint16_t* b, size_t lenB) {
	if (lenA != lenB || lenA == 0)
		return false;
	uint16_t minA = *std::min_element(a, a + lenA);
	uint16_t minB = *std::min_element(b, b + lenB);
	return durationsMatch(minA, minB);
}

}  // namespace learn
