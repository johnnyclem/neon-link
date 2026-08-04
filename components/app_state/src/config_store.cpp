#include "app_state/config_store.h"

#include "esp_log.h"
#include "halesp/storage_nvs.hpp"

#include "app_state/timeline_bus.h"

namespace {

const char* kTag = "config";
const char* kKey = "cfg";
constexpr int64_t kSaveDebounceUs = 2000000;

halesp::StorageNvs g_storage;
neon::Config g_config;
bool g_save_pending = false;
int64_t g_last_change_us = 0;

bool persist(const neon::Config& cfg) {
  uint8_t buf[512];
  const size_t n = neon::config_encode(cfg, buf, sizeof(buf));
  return n != 0 && g_storage.write_blob(kKey, buf, n);
}

}  // namespace

void neon_config_load() {
  uint8_t buf[512];
  size_t len = 0;
  if (g_storage.read_blob(kKey, buf, sizeof(buf), &len) &&
      neon::config_decode(buf, len, &g_config)) {
    ESP_LOGI(kTag, "config loaded (%u bytes)", static_cast<unsigned>(len));
  } else {
    g_config = neon::Config{};
    neon::config_sanitize(&g_config);
    ESP_LOGW(kTag, "no valid stored config; using defaults");
  }
  engine_config_bus().publish(g_config.engine);
}

const neon::Config& neon_config() { return g_config; }

void neon_config_apply(const neon::Config& cfg) {
  neon::Config clean = cfg;
  neon::config_sanitize(&clean);
  g_config = clean;
  engine_config_bus().publish(g_config.engine);
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

bool neon_config_save(const neon::Config& cfg) {
  neon::Config clean = cfg;
  neon::config_sanitize(&clean);
  if (!persist(clean)) {
    return false;
  }
  g_config = clean;
  engine_config_bus().publish(g_config.engine);
  g_save_pending = false;
  return true;
}
