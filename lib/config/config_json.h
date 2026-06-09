#pragma once

#include <cstddef>
#include <string>

#include "config_model.h"

namespace config {

// Serialize to exportable JSON (also the persisted payload); false if over MAX_CONFIG_BYTES.
bool toJson(const Config& cfg, std::string& out);

// Parse exportable JSON onto defaults; false on oversize, malformed, or too-deep input.
bool fromJson(const char* data, size_t len, Config& out);

}  // namespace config
