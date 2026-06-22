#pragma once

#include <esp_http_server.h>

#include "config_model.h"

namespace web {

// Seed the config API; call once from setup() before the server starts.
void configApiBegin(const config::Config& cfg);

// Register the config/status/learn endpoints; before the catch-all, like registerAuthApi.
void registerConfigApi(httpd_handle_t server);

// Drain a pending apply/restart/factory-reset on the loop task (never httpd). Call from loop().
void pollConfigApply();

// Advance the learn capture on the loop task (never httpd); touches the driver + RMT. Call from loop().
void pollLearnApi();

}  // namespace web
