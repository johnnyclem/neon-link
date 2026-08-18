#pragma once

#include <cstdint>

#include "neon/config/model.hpp"

// Scratch size for an encoded config blob, with headroom so appending a
// field does not silently start failing to persist.
inline constexpr size_t kConfigBlobBuf = 2048;
static_assert(sizeof(neon::Config) + 64 < kConfigBlobBuf,
              "config blob no longer fits its scratch buffer");

// Loads the persisted config from NVS at boot (defaults on absence or
// corruption). Call once before the tasks that consume it start.
void neon_config_load();

// The active configuration (read-mostly; mutate through
// neon_config_apply).
const neon::Config& neon_config();

// Adopt a new configuration: sanitize, publish the engine portion to
// core 1 via the config bus, and schedule a debounced NVS write.
void neon_config_apply(const neon::Config& cfg);

// G6: while held, neon_config_flush will not touch flash. The audio
// task holds this for the whole time I2S DMA is running. Idle (I2S
// down, including never started) still commits after 2 s of quiet.
void neon_config_hold_nvs(bool hold);

// Call periodically (any core-0 service loop): persists to NVS once the
// config has been quiet for 2 s after a change, unless NVS is held.
void neon_config_flush(int64_t now_us);

// Write any debounced pending config to NVS immediately (reboot / OTA).
// Ignores the hold — caller must accept a flash stall or stop I2S first.
bool neon_config_flush_now();

// Adopt + persist if NVS is not held; otherwise adopt and queue (G6).
bool neon_config_save(const neon::Config& cfg);

// Generation counter. Increments on neon_config_apply and
// neon_config_save (not on the later NVS flush of an already-applied
// change). Starts at 0 after boot / load. Emitted as "rev" on
// GET /api/status so the plugin can notice OLED / web / MIDI-PC edits
// without a deep JSON compare.
uint32_t neon_config_rev();

// Preset slots (milestone 9): full-config snapshots in NVS, recalled from
// the web editor or via MIDI Program Change. slot is 0..3.
inline constexpr int kPresetSlots = 4;
bool neon_preset_save(int slot);
bool neon_preset_recall(int slot);  // applies live (WiFi fields excluded)

// Erase the stored config and every preset slot, returning the module to
// the state it shipped in. The caller reboots afterwards.
bool neon_config_factory_reset();
