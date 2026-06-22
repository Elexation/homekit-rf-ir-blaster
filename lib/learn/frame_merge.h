#pragma once

#include <cstddef>
#include <cstdint>

namespace learn {

// Spaces >= this delimit repeated frames; shorter spaces fall within a frame.
constexpr uint16_t kResetGapMinUs = 3000;

// Default frame-repeat when only one frame was captured.
constexpr size_t kStoredFrameCount = 8;

// Merge the repeated frames of one OOK burst into one clean frame, undoing gain
// clipping by taking the longest mark / shortest space per slot. Returns the frame
// (odd length, ends on a mark) in `out` and its length; sets `*resetGapUs` to the
// largest inter-frame gap and `*outFrames` to the frame count. 0 if unusable.
size_t declipBurst(const uint16_t* in, size_t len,
                   uint16_t* out, size_t outCap, uint16_t* resetGapUs,
                   uint16_t* outFrames = nullptr);

}  // namespace learn
