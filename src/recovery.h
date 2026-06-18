#pragma once

#include "config_model.h"

namespace recovery {

// Call in setup() before web::begin, on a COPY of settings (overwritten in safe mode).
// Safe mode triggers on the 3rd unhealthy boot or a prior long-press.
void begin(config::Settings& settings);

bool inSafeMode();

// Call every loop(): clears the boot counter after a stable run, drives BOOT-button recovery.
void poll();

}  // namespace recovery
