#pragma once

#include <esp_http_server.h>

#include "config_model.h"

namespace web {

// Seed the config API (editable copy, NVS store, rev counter). Call once from setup(),
// before the WiFi callback can start the server.
void configApiBegin(const config::Config& cfg);

// Register the config/status/learn endpoints; before the catch-all, like registerAuthApi.
void registerConfigApi(httpd_handle_t server);

// Drain a pending live-apply/restart/factory-reset; runs HomeSpan mutations on the
// loop task (never the httpd task). Call from loop().
void pollConfigApply();

}  // namespace web
