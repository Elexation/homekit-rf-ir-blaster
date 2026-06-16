#pragma once

#include "config_model.h"

namespace web {

// Copy the settings for the deferred start(); call from setup(), before WiFi connects.
void begin(const config::Settings& settings);

// Start the listeners from the stashed settings. Wired to the HomeSpan connection
// callback (fires on every reconnect), so it runs only once.
void start(int);

}  // namespace web
