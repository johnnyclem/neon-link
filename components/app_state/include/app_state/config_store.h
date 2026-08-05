#pragma once

#include <cstdint>

#include "neon/config/model.hpp"

// Loads the persisted config from NVS at boot (defaults on absence or
// corruption). Call once before the tasks that consume it start.
void neon_config_load();

// The active configuration (read-mostly; mutate through
// neon_config_apply).
const neon::Config& neon_config();

// Adopt a new configuration: sanitize, publish the engine portion to
// core 1 via the config bus, and schedule a debounced NVS write.
void neon_config_apply(const neon::Config& cfg);

// Call periodically (any core-0 service loop): persists to NVS once the
// config has been quiet for 2 s after a change.
void neon_config_flush(int64_t now_us);

// Immediate persist + adopt (used at explicit save points).
bool neon_config_save(const neon::Config& cfg);

// Preset slots (milestone 9): full-config snapshots in NVS, recalled from
// the web editor or via MIDI Program Change. slot is 0..3.
inline constexpr int kPresetSlots = 4;
bool neon_preset_save(int slot);
bool neon_preset_recall(int slot);  // applies live (WiFi fields excluded)
