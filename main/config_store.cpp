#include "config_store.h"

#include "esp_log.h"
#include "halesp/storage_nvs.hpp"

namespace {

const char* kTag = "config";
const char* kKey = "cfg";

halesp::StorageNvs g_storage;
neon::Config g_config;

}  // namespace

void neon_config_load() {
  uint8_t buf[512];
  static_assert(sizeof(buf) >= 0, "");
  size_t len = 0;
  if (g_storage.read_blob(kKey, buf, sizeof(buf), &len) &&
      neon::config_decode(buf, len, &g_config)) {
    ESP_LOGI(kTag, "config loaded (%u bytes)", static_cast<unsigned>(len));
    return;
  }
  g_config = neon::Config{};
  neon::config_sanitize(&g_config);
  ESP_LOGW(kTag, "no valid stored config; using defaults");
}

const neon::Config& neon_config() { return g_config; }

bool neon_config_save(const neon::Config& cfg) {
  neon::Config clean = cfg;
  neon::config_sanitize(&clean);
  uint8_t buf[512];
  const size_t n = neon::config_encode(clean, buf, sizeof(buf));
  if (n == 0 || !g_storage.write_blob(kKey, buf, n)) {
    return false;
  }
  g_config = clean;
  return true;
}
