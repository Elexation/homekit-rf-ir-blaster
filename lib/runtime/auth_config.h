#pragma once

#include <cstddef>
#include <cstdint>

namespace runtime {

// Hardcoded auth tunables, never exposed via Settings or the GUI.
constexpr uint32_t kPbkdf2DefaultIterations = 150000;
constexpr size_t   kSaltLen                 = 16;
constexpr size_t   kDerivedKeyLen           = 32;

constexpr uint64_t kIdleTimeoutMs     = 900000ull;    // 15 minutes
constexpr uint64_t kAbsoluteTimeoutMs = 28800000ull;  // 8 hours

}  // namespace runtime
