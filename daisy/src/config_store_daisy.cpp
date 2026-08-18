// Daisy implementation of app_state/config_store.h: the same encoded
// blob (magic + version + CRC) the ESP32 keeps in NVS and the Teensy in
// LittleFS, stored in raw QSPI flash sectors. Same debounce, same rev
// counter, same preset semantics. A corrupt or blank sector fails the
// CRC and cleanly falls back to defaults.
//
// Layout: five 4 KB sectors at the top quarter of the 8 MB chip —
// config at kBaseOffset, presets 0..3 in the sectors after it. The app
// image executes XIP from internal flash (APP_TYPE=BOOT_NONE), so
// erasing/programming QSPI at runtime is safe; reads go through the
// memory-mapped window (QSPIHandle restores memory-mapped mode after a
// write). Never combine this store with APP_TYPE=BOOT_QSPI.

#include "app_state/config_store.h"

#include <cstring>

#include "app_state/audio_bus.h"
#include "app_state/timeline_bus.h"

#include "board_daisy.h"
#include "config_store_daisy.h"
#include "timebase_daisy.h"

namespace {

constexpr int64_t kSaveDebounceUs = 2000000;
constexpr uint32_t kSectorBytes = 4096;
constexpr uint32_t kBaseOffset = 0x00780000;  // 7.5 MB into the 8 MB chip
static_assert(kConfigBlobBuf <= kSectorBytes, "blob must fit one sector");

// Worst-case stall a persist can cause (sector erase + page programs);
// the pre-persist hook is asked to cover this much schedule.
constexpr int64_t kPersistStallBudgetUs = 800000;

neon::Config g_config;
bool g_save_pending = false;
int64_t g_last_change_us = 0;
uint32_t g_rev = 0;
void (*g_pre_persist)(int64_t) = nullptr;

uint32_t slot_offset(int slot) {  // slot -1 = config, 0..3 = presets
  return kBaseOffset + static_cast<uint32_t>(slot + 1) * kSectorBytes;
}

void publish_buses() {
  engine_config_bus().publish(g_config.engine);
  audio_config_bus().publish(neon::audio_engine_config(g_config));
}

bool write_slot(int slot, const uint8_t* data, size_t len) {
  if (g_pre_persist != nullptr) {
    g_pre_persist(kPersistStallBudgetUs);
  }
  const uint32_t off = slot_offset(slot);
  if (board_qspi().Erase(off, off + kSectorBytes) !=
      daisy::QSPIHandle::Result::OK) {
    return false;
  }
  return board_qspi().Write(off, static_cast<uint32_t>(len),
                           const_cast<uint8_t*>(data)) ==
         daisy::QSPIHandle::Result::OK;
}

bool read_slot(int slot, uint8_t* buf, size_t cap, size_t* len) {
  const void* src = board_qspi().GetData(slot_offset(slot));
  if (src == nullptr) {
    return false;
  }
  std::memcpy(buf, src, cap);
  *len = cap;  // config_decode reads the header's own payload size
  return true;
}

bool persist(const neon::Config& cfg) {
  uint8_t buf[kConfigBlobBuf];
  const size_t n = neon::config_encode(cfg, buf, sizeof(buf));
  return n != 0 && write_slot(-1, buf, n);
}

}  // namespace

void neon_daisy_set_pre_persist_hook(void (*fn)(int64_t)) {
  g_pre_persist = fn;
}

void neon_config_load() {
  uint8_t buf[kConfigBlobBuf];
  size_t len = 0;
  neon::Config loaded;
  if (read_slot(-1, buf, sizeof(buf), &len) &&
      neon::config_decode(buf, len, &loaded)) {
    g_config = loaded;
  } else {
    g_config = neon::Config{};
    neon::config_sanitize(&g_config);
  }
  publish_buses();
}

const neon::Config& neon_config() { return g_config; }

void neon_config_apply(const neon::Config& cfg) {
  g_config = cfg;
  neon::config_sanitize(&g_config);
  publish_buses();
  g_save_pending = true;
  g_last_change_us = 0;  // stamped by the next flush() call
  ++g_rev;
}

void neon_config_hold_nvs(bool) {}

void neon_config_flush(int64_t now_us) {
  if (!g_save_pending) {
    return;
  }
  if (g_last_change_us == 0) {
    g_last_change_us = now_us;
    return;
  }
  if (now_us - g_last_change_us < kSaveDebounceUs) {
    return;
  }
  g_save_pending = false;
  persist(g_config);
}

bool neon_config_flush_now() {
  if (!g_save_pending) {
    return true;
  }
  g_save_pending = false;
  return persist(g_config);
}

bool neon_config_save(const neon::Config& cfg) {
  g_config = cfg;
  neon::config_sanitize(&g_config);
  publish_buses();
  ++g_rev;
  g_save_pending = false;
  return persist(g_config);
}

uint32_t neon_config_rev() { return g_rev; }

bool neon_preset_save(int slot) {
  if (slot < 0 || slot >= kPresetSlots) {
    return false;
  }
  uint8_t buf[kConfigBlobBuf];
  const size_t n = neon::config_encode(g_config, buf, sizeof(buf));
  return n != 0 && write_slot(slot, buf, n);
}

bool neon_preset_recall(int slot) {
  if (slot < 0 || slot >= kPresetSlots) {
    return false;
  }
  uint8_t buf[kConfigBlobBuf];
  size_t len = 0;
  neon::Config preset;
  if (!read_slot(slot, buf, sizeof(buf), &len) ||
      !neon::config_decode(buf, len, &preset)) {
    return false;
  }
  // Presets are performance snapshots: keep the current network identity
  // (mirrors the ESP32 and Teensy stores; the WiFi/AP fields are inert
  // here but kept in step so a preset file moves between targets
  // cleanly).
  std::memcpy(preset.wifi, g_config.wifi, sizeof(preset.wifi));
  preset.wifi_retries = g_config.wifi_retries;
  preset.ap_policy = g_config.ap_policy;
  preset.ap_require_pass = g_config.ap_require_pass;
  preset.ap_hidden = g_config.ap_hidden;
  preset.ap_channel = g_config.ap_channel;
  std::memcpy(preset.ap_ssid, g_config.ap_ssid, sizeof(preset.ap_ssid));
  std::memcpy(preset.ap_pass, g_config.ap_pass, sizeof(preset.ap_pass));
  std::memcpy(preset.device_name, g_config.device_name,
              sizeof(preset.device_name));
  neon_config_apply(preset);
  return true;
}

bool neon_config_factory_reset() {
  // One erase spans all five sectors (config + presets).
  if (g_pre_persist != nullptr) {
    g_pre_persist(kPersistStallBudgetUs * (1 + kPresetSlots));
  }
  const bool ok = board_qspi().Erase(slot_offset(-1),
                                    slot_offset(kPresetSlots)) ==
                  daisy::QSPIHandle::Result::OK;
  g_config = neon::Config{};
  neon::config_sanitize(&g_config);
  publish_buses();
  g_save_pending = false;
  ++g_rev;
  return ok;
}
