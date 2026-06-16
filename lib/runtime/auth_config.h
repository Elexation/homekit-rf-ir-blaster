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

constexpr char kSessionCookieName[] = "__Host-SID";
constexpr char kCsrfCookieName[]    = "__Host-CSRF";

// Plain-HTTP flavor: the __Host- prefix mandates Secure, which a browser drops over HTTP,
// so plain mode (safe mode, https off) uses these unprefixed names with no Secure attribute.
constexpr char kSessionCookieNamePlain[] = "SID";
constexpr char kCsrfCookieNamePlain[]    = "CSRF";

}  // namespace runtime
