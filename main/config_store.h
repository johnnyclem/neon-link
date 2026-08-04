#pragma once

#include "neon/config/model.hpp"

// Loads the persisted config from NVS at boot (defaults on absence or
// corruption). Call once before the tasks that consume it start.
void neon_config_load();

// The active configuration (read-mostly after boot; milestones 6/8 add
// live mutation through a config queue).
const neon::Config& neon_config();

// Persist the given config to NVS and adopt it as active.
bool neon_config_save(const neon::Config& cfg);
