// Teensy implementation of app_state/config_store.h: the same encoded
// blob (magic + version + CRC) the ESP32 keeps in NVS, stored as files
// in a LittleFS filesystem carved from the top of the 8 MB program
// flash. Same debounce, same rev counter, same preset semantics.

#include "app_state/config_store.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <cstdio>
#include <cstring>

#include "app_state/audio_bus.h"
#include "app_state/timeline_bus.h"

namespace {

constexpr int64_t kSaveDebounceUs = 2000000;
constexpr size_t kFsBytes = 1 * 1024 * 1024;  // top 1 MB of program flash
const char* kConfigPath = "/config.bin";

LittleFS_Program g_fs;
bool g_fs_ok = false;
neon::Config g_config;
bool g_save_pending = false;
int64_t g_last_change_us = 0;
uint32_t g_rev = 0;

void publish_buses() {
  engine_config_bus().publish(g_config.engine);
  audio_config_bus().publish(neon::audio_engine_config(g_config));
}

const char* preset_path(int slot) {
  static char path[16];
  std::snprintf(path, sizeof(path), "/preset%d.bin", slot);
  return path;
}

bool write_file(const char* path, const uint8_t* data, size_t len) {
  if (!g_fs_ok) {
    return false;
  }
  g_fs.remove(path);
  File f = g_fs.open(path, FILE_WRITE);
  if (!f) {
    return false;
  }
  const size_t n = f.write(data, len);
  f.close();
  return n == len;
}

bool read_file(const char* path, uint8_t* buf, size_t cap, size_t* len) {
  if (!g_fs_ok) {
    return false;
  }
  File f = g_fs.open(path, FILE_READ);
  if (!f) {
    return false;
  }
  const size_t n = f.read(buf, cap);
  f.close();
  *len = n;
  return n != 0;
}

bool persist(const neon::Config& cfg) {
  uint8_t buf[kConfigBlobBuf];
  const size_t n = neon::config_encode(cfg, buf, sizeof(buf));
  return n != 0 && write_file(kConfigPath, buf, n);
}

}  // namespace

void neon_config_load() {
  g_fs_ok = g_fs.begin(kFsBytes);
  if (!g_fs_ok) {
    Serial.println("config: LittleFS begin failed; running on defaults");
  }
  uint8_t buf[kConfigBlobBuf];
  size_t len = 0;
  neon::Config loaded;
  if (read_file(kConfigPath, buf, sizeof(buf), &len) &&
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
  if (!persist(g_config)) {
    Serial.println("config: persist failed");
  }
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
  return n != 0 && write_file(preset_path(slot), buf, n);
}

bool neon_preset_recall(int slot) {
  if (slot < 0 || slot >= kPresetSlots) {
    return false;
  }
  uint8_t buf[kConfigBlobBuf];
  size_t len = 0;
  neon::Config preset;
  if (!read_file(preset_path(slot), buf, sizeof(buf), &len) ||
      !neon::config_decode(buf, len, &preset)) {
    return false;
  }
  // Presets are performance snapshots: keep the current network identity
  // (mirrors the ESP32 store; WiFi fields are inert here but kept in
  // step so a preset file moves between targets cleanly).
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
  bool ok = true;
  if (g_fs_ok) {
    ok = g_fs.remove(kConfigPath);
    for (int i = 0; i < kPresetSlots; ++i) {
      g_fs.remove(preset_path(i));
    }
  }
  g_config = neon::Config{};
  neon::config_sanitize(&g_config);
  publish_buses();
  g_save_pending = false;
  ++g_rev;
  return ok;
}
