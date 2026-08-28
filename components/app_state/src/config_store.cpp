#include "app_state/config_store.h"

#include <cstring>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "board_mac.h"
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
bool g_nvs_hold = false;
int64_t g_last_change_us = 0;
uint32_t g_rev = 0;

bool persist(const neon::Config& cfg) {
  uint8_t buf[kConfigBlobBuf];
  const size_t n = neon::config_encode(cfg, buf, sizeof(buf));
  return n != 0 && g_storage.write_blob(kKey, buf, n);
}

// True first boot only (G1 in the ship-gate review): the setup AP's
// password, derived from the MAC rather than left at the struct-literal
// "link1234" — a shared, documented default is one Google search away
// from an open AP on every unit this batch ships, in someone else's
// house. Not re-derived on a config that already loaded: the owner may
// have changed it since, and there is nothing here worth overwriting.
void provision_ap_pass_from_mac(neon::Config* cfg) {
  uint8_t mac[6] = {};
  neon_read_unit_mac(mac);
  neon::derive_ap_pass_from_mac(mac, cfg->ap_pass, sizeof(cfg->ap_pass));
}

// The random token gating POST /api/ota and POST /api/factory_reset.
// Generated whenever it is missing, not only on a true first boot: a unit
// already in the field OTA-updating from a firmware that predates
// device_token decodes its config successfully (config_decode zeroes the
// field for it, the tail-padding trap the v3/v4 migrations hit too) and
// would otherwise sit with an empty, always-matching token forever.
void provision_device_token(neon::Config* cfg) {
  uint8_t token_bytes[16];
  esp_fill_random(token_bytes, sizeof(token_bytes));
  static const char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(token_bytes); ++i) {
    cfg->device_token[i * 2] = kHex[(token_bytes[i] >> 4) & 0xf];
    cfg->device_token[i * 2 + 1] = kHex[token_bytes[i] & 0xf];
  }
  cfg->device_token[sizeof(token_bytes) * 2] = '\0';
}

}  // namespace

void neon_config_load() {
  uint8_t buf[kConfigBlobBuf];
  size_t len = 0;
  const bool loaded = g_storage.read_blob(kKey, buf, sizeof(buf), &len) &&
                      neon::config_decode(buf, len, &g_config);
  if (loaded) {
    ESP_LOGI(kTag, "config loaded (%u bytes)", static_cast<unsigned>(len));
  } else {
    g_config = neon::Config{};
    neon::config_sanitize(&g_config);
    provision_ap_pass_from_mac(&g_config);
    ESP_LOGW(kTag, "no valid stored config; using defaults");
  }
  // Runs on a true first boot (device_token is still "") and on an
  // OTA upgrade from a firmware old enough not to have had one — either
  // way, the OTA/factory-reset gate needs a real token before it is safe
  // to serve those endpoints at all.
  const bool needed_token = g_config.device_token[0] == '\0';
  if (needed_token) {
    provision_device_token(&g_config);
    neon::config_sanitize(&g_config);
    ESP_LOGW(kTag, "generated a device token (see the OLED Network page) — "
                   "the web editor needs it to install updates");
  }
  if ((!loaded || needed_token) && !persist(g_config)) {
    // Not fatal — the secrets still apply for this boot from RAM — but
    // worth shouting about: one that never reaches NVS will look
    // identical to one that did until the next reboot generates another,
    // and by then whatever read the old one off the OLED is stale.
    ESP_LOGE(kTag, "first-boot secrets could not be saved to NVS");
  }
  publish_buses();
}

const neon::Config& neon_config() { return g_config; }

void neon_config_apply(const neon::Config& cfg) {
  neon::Config clean = cfg;
  neon::config_sanitize(&clean);
  const bool net = neon::network_identity_changed(g_config, clean);
  g_config = clean;
  publish_buses();
  ++g_rev;
  // G6: a friend Save of WiFi/AP identity must hit flash even while
  // I2S is holding. RAM-only apply looks like it worked, then reboot
  // reloads always/open. One NVS stall is better than a silent revert.
  if (net) {
    g_save_pending = false;
    g_last_change_us = 0;
    if (persist(g_config)) {
      ESP_LOGI(kTag, "config saved (network identity)");
    } else {
      g_save_pending = true;
      ESP_LOGE(kTag, "config save failed");
    }
    return;
  }
  g_save_pending = true;
  g_last_change_us = 0;
}

void neon_config_apply_ram(const neon::Config& cfg) {
  neon::Config clean = cfg;
  neon::config_sanitize(&clean);
  g_config = clean;
  publish_buses();
  ++g_rev;
  // Deliberately no persist and no g_save_pending: this is a trial value
  // (e.g. a WiFi credential being tested on-device). The caller persists
  // with neon_config_save() only after it is proven good. Leaving
  // g_save_pending untouched also means a later debounced flush cannot
  // sneak the untrusted value to flash.
}

void neon_config_hold_nvs(bool hold) {
  g_nvs_hold = hold;
}

void neon_config_flush(int64_t now_us) {
  if (!g_save_pending) {
    return;
  }
  if (g_nvs_hold) {
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
  const bool net = neon::network_identity_changed(g_config, clean);
  if (g_nvs_hold && !net) {
    g_config = clean;
    publish_buses();
    g_save_pending = true;
    g_last_change_us = 0;
    ++g_rev;
    return true;
  }
  if (!persist(clean)) {
    return false;
  }
  g_config = clean;
  publish_buses();
  g_save_pending = false;
  g_last_change_us = 0;
  ++g_rev;
  ESP_LOGI(kTag, "config saved (ssid=\"%s\" pass_len=%u)", g_config.wifi[0].ssid,
           static_cast<unsigned>(std::strlen(g_config.wifi[0].pass)));
  return true;
}

uint32_t neon_config_rev() { return g_rev; }

bool neon_config_factory_reset() {
  g_save_pending = false;
  g_last_change_us = 0;
  const bool ok = g_storage.erase_all();
  g_config = neon::Config{};
  neon::config_sanitize(&g_config);
  publish_buses();
  ++g_rev;
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
