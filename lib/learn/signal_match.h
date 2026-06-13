#pragma once

#include <cstddef>
#include <cstdint>

#include "config_model.h"

namespace learn {

// Armed in parallel; the first to accumulate a qualifying match wins the band.
enum class Source : uint8_t { Rf315 = 0, Rf433 = 1, Ir = 2 };

// Microsecond mark/space durations; a burst ends on its final mark, so counts are odd until a trailing gap is stored.

// Below this never matches (ditto frames, stray edges) but still counts as activity.
constexpr size_t kMinBurstPulses = 16;
// One entry reserved for the appended trailing gap, to fit the config ceiling.
constexpr size_t kMaxBurstPulses = config::MAX_PULSES - 1;

// Match within 25% of the larger value or 100 us, whichever is wider.
constexpr uint32_t kMatchTolerancePercent = 25;
constexpr uint32_t kMatchToleranceUs      = 100;

// IR carrier window; duplicates config_validate.cpp, kept in sync by a test.
constexpr uint16_t kIrCarrierMinHz = 20000;
constexpr uint16_t kIrCarrierMaxHz = 60000;

bool durationsMatch(uint16_t a, uint16_t b);

// Length bounds and no zero durations; carrierHz is checked only for Ir.
bool wellFormed(Source src, const uint16_t* pulses, size_t len, uint16_t carrierHz);

// Same length and every entry within tolerance.
bool burstsMatch(const uint16_t* a, size_t lenA, const uint16_t* b, size_t lenB);

// Same length and matching shortest pulse: same frame, different payload.
bool structurallyAlike(const uint16_t* a, size_t lenA, const uint16_t* b, size_t lenB);

}  // namespace learn
