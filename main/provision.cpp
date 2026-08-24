#include "provision.h"

#include "sdkconfig.h"

#if CONFIG_NEON_LINKSYNC && !CONFIG_NEON_BOARD_LINKSYNC_P4LCD && !CONFIG_NEON_BOARD_LINKSYNC_TAB5 && !CONFIG_NEON_BOARD_LINKSYNC_C3OLED

#include <cstdio>
#include <cstring>

#include "app_state/config_store.h"
#include "board_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "wifi.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

namespace {

const char* kTag = "prov";
constexpr int64_t kSoftApFallbackUs = 90000000;  // 90 s

bool g_active = false;
bool g_inited = false;
int64_t g_started_us = 0;
bool g_fallback_started = false;

void apply_creds(const wifi_sta_config_t& sta) {
  neon::Config cfg = neon_config();
  std::memset(cfg.wifi[0].ssid, 0, sizeof(cfg.wifi[0].ssid));
  std::memset(cfg.wifi[0].pass, 0, sizeof(cfg.wifi[0].pass));
  std::memcpy(cfg.wifi[0].ssid, sta.ssid,
              sizeof(cfg.wifi[0].ssid) - 1);
  std::memcpy(cfg.wifi[0].pass, sta.password,
              sizeof(cfg.wifi[0].pass) - 1);
  neon_config_apply(cfg);
  ESP_LOGI(kTag, "stored STA \"%s\"", cfg.wifi[0].ssid);
}

void on_prov_event(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base != WIFI_PROV_EVENT) {
    return;
  }
  switch (id) {
    case WIFI_PROV_START:
      ESP_LOGI(kTag, "BLE provisioning started");
      break;
    case WIFI_PROV_CRED_RECV: {
      auto* sta = static_cast<wifi_sta_config_t*>(data);
      if (sta != nullptr) {
        apply_creds(*sta);
      }
      break;
    }
    case WIFI_PROV_CRED_FAIL:
      ESP_LOGW(kTag, "provisioning credentials rejected");
      break;
    case WIFI_PROV_CRED_SUCCESS:
      ESP_LOGI(kTag, "provisioning succeeded");
      break;
    case WIFI_PROV_END:
      g_active = false;
      wifi_prov_mgr_deinit();
      g_inited = false;
      if (neon_wifi_has_credentials()) {
        neon_wifi_apply_credentials();
      }
      ESP_LOGI(kTag, "BLE provisioning ended");
      break;
    default:
      break;
  }
}

}  // namespace

void neon_provision_start() {
  if (neon_wifi_has_credentials()) {
    ESP_LOGI(kTag, "already have STA credentials; skipping BLE provision");
    return;
  }
  if (g_active) {
    return;
  }

  wifi_prov_mgr_config_t cfg = {};
  cfg.scheme = wifi_prov_scheme_ble;
  cfg.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
  if (wifi_prov_mgr_init(cfg) != ESP_OK) {
    ESP_LOGE(kTag, "wifi_prov_mgr_init failed");
    return;
  }
  g_inited = true;

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                             &on_prov_event, nullptr));

  uint8_t mac[6] = {};
  neon_read_unit_mac(mac);
  char name[13] = {};
  std::snprintf(name, sizeof(name), "LSYNC-%02X%02X", mac[4], mac[5]);
  char pop[16] = {};
  neon::derive_ap_pass_from_mac(mac, pop, sizeof(pop));

  ESP_LOGI(kTag,
           "BLE provision: open \"ESP BLE Prov\" and join %s (PoP %s)",
           name, pop);
  if (wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, pop, name,
                                       nullptr) != ESP_OK) {
    ESP_LOGE(kTag, "start_provisioning failed");
    wifi_prov_mgr_deinit();
    g_inited = false;
    return;
  }
  g_active = true;
  g_started_us = esp_timer_get_time();
}

bool neon_provision_active() { return g_active; }

bool neon_provision_poll(int64_t now_us) {
  if (!g_active || g_fallback_started) {
    return false;
  }
  if (now_us - g_started_us < kSoftApFallbackUs) {
    return false;
  }
  g_fallback_started = true;
  ESP_LOGW(kTag, "BLE provision still open after 90 s; raising SoftAP");
  return true;
}

void neon_provision_stop() {
  if (!g_inited && !g_active) {
    return;
  }
  if (g_active) {
    wifi_prov_mgr_stop_provisioning();
  }
  if (g_inited) {
    wifi_prov_mgr_deinit();
  }
  g_active = false;
  g_inited = false;
}

#elif CONFIG_NEON_BOARD_LINKSYNC_P4LCD || CONFIG_NEON_BOARD_LINKSYNC_TAB5 || CONFIG_NEON_BOARD_LINKSYNC_C3OLED

#include "esp_log.h"

void neon_provision_start() {
#if CONFIG_NEON_BOARD_LINKSYNC_C3OLED
  ESP_LOGI("prov", "C3 OLED: SoftAP provision (no BLE)");
#else
  ESP_LOGI("prov", "P4 LCD: SoftAP provision (no BLE)");
#endif
}
bool neon_provision_active() { return false; }
bool neon_provision_poll(int64_t) { return false; }
void neon_provision_stop() {}

#else

void neon_provision_start() {}
bool neon_provision_active() { return false; }
bool neon_provision_poll(int64_t) { return false; }
void neon_provision_stop() {}

#endif
