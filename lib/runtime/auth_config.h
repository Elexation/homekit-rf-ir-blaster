#pragma once

#include <cstddef>
#include <cstdint>

namespace runtime {

// Hardcoded auth tunables, never exposed via Settings or the GUI.
constexpr uint32_t kPbkdf2DefaultIterations = 150000;
constexpr size_t   kSaltLen                 = 16;
constexpr size_t   kDerivedKeyLen           = 32;

}  // namespace runtime
