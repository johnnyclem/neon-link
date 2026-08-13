#include "app_state/config_store.h"

#include <cstring>

#include "esp_log.h"
#include "halesp/storage_nvs.hpp"

#include "app_state/audio_bus.h"
#include "app_state/timeline_bus.h"

namespace {

const char* kTag = "config";
const char* kKey = "cfg";
constexpr int64_t kSaveDebounceUs = 2000000;

halesp::StorageNvs g_storage;
neon::Config g_config;

// Both core-1 consumers see a config change through their own seqlock:
// the pulse engine reads engine_config_bus, the audio task reads
// audio_config_bus. Publishing them together keeps them from disagreeing
// about a change for a block or two.
void publish_buses() {
  engine_config_bus().publish(g_config.engine);
  audio_config_bus().publish(neon::audio_engine_config(g_config));
}

bool g_save_pending = false;
int64_t g_last_change_us = 0;

bool persist(const neon::Config& cfg) {
  uint8_t buf[kConfigBlobBuf];
  const size_t n = neon::config_encode(cfg, buf, sizeof(buf));
  return n != 0 && g_storage.write_blob(kKey, buf, n);
}

}  // namespace

void neon_config_load() {
  uint8_t buf[kConfigBlobBuf];
  size_t len = 0;
  if (g_storage.read_blob(kKey, buf, sizeof(buf), &len) &&
      neon::config_decode(buf, len, &g_config)) {
    ESP_LOGI(kTag, "config loaded (%u bytes)", static_cast<unsigned>(len));
  } else {
    g_config = neon::Config{};
    neon::config_sanitize(&g_config);
    ESP_LOGW(kTag, "no valid stored config; using defaults");
  }
  publish_buses();
}

const neon::Config& neon_config() { return g_config; }

void neon_config_apply(const neon::Config& cfg) {
  neon::Config clean = cfg;
  neon::config_sanitize(&clean);
  g_config = clean;
  publish_buses();
  g_save_pending = true;
  g_last_change_us = 0;  // stamped by the next flush call
}

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
  g_last_change_us = 0;
  if (persist(g_config)) {
    ESP_LOGI(kTag, "config saved");
  } else {
    ESP_LOGE(kTag, "config save failed");
  }
}

bool neon_config_flush_now() {
  if (!g_save_pending) {
    return true;
  }
  g_save_pending = false;
  g_last_change_us = 0;
  if (persist(g_config)) {
    ESP_LOGI(kTag, "config saved (immediate)");
    return true;
  }
  ESP_LOGE(kTag, "config save failed");
  return false;
}

bool neon_config_save(const neon::Config& cfg) {
  neon::Config clean = cfg;
  neon::config_sanitize(&clean);
  if (!persist(clean)) {
    return false;
  }
  g_config = clean;
  publish_buses();
  g_save_pending = false;
  g_last_change_us = 0;
  ESP_LOGI(kTag, "config saved (ssid=\"%s\" pass_len=%u)", g_config.wifi[0].ssid,
           static_cast<unsigned>(std::strlen(g_config.wifi[0].pass)));
  return true;
}

bool neon_config_factory_reset() {
  g_save_pending = false;
  g_last_change_us = 0;
  const bool ok = g_storage.erase_all();
  g_config = neon::Config{};
  neon::config_sanitize(&g_config);
  publish_buses();
  ESP_LOGW(kTag, "factory reset %s", ok ? "complete" : "FAILED (NVS error)");
  return ok;
}

namespace {
const char* preset_key(int slot) {
  static const char* kKeys[kPresetSlots] = {"p0", "p1", "p2", "p3"};
  return kKeys[slot];
}
}  // namespace

bool neon_preset_save(int slot) {
  if (slot < 0 || slot >= kPresetSlots) {
    return false;
  }
  uint8_t buf[kConfigBlobBuf];
  const size_t n = neon::config_encode(g_config, buf, sizeof(buf));
  if (n == 0 || !g_storage.write_blob(preset_key(slot), buf, n)) {
    return false;
  }
  ESP_LOGI(kTag, "preset %d saved", slot);
  return true;
}

bool neon_preset_recall(int slot) {
  if (slot < 0 || slot >= kPresetSlots) {
    return false;
  }
  uint8_t buf[kConfigBlobBuf];
  size_t len = 0;
  neon::Config preset;
  if (!g_storage.read_blob(preset_key(slot), buf, sizeof(buf), &len) ||
      !neon::config_decode(buf, len, &preset)) {
    ESP_LOGW(kTag, "preset %d empty or invalid", slot);
    return false;
  }
  // Presets are performance snapshots: keep the current network identity
  // (stored networks, access point, and device name) exactly as it is.
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
  ESP_LOGI(kTag, "preset %d recalled", slot);
  return true;
}
