#include "wifi.h"

#include <cstring>

#include "app_state/config_store.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "netman/net_manager.h"

namespace {

const char* kTag = "wifi";
constexpr EventBits_t kGotIpBit = BIT0;

EventGroupHandle_t g_events = nullptr;
bool g_handlers_registered = false;
bool g_sta_started = false;
bool g_connect_requested = false;
uint8_t g_last_disconnect_reason = 0;

// Stored config wins; menuconfig is the fallback for development builds.
const char* effective_ssid() {
  return neon_config().wifi_ssid[0] != '\0' ? neon_config().wifi_ssid
                                            : CONFIG_NEON_WIFI_SSID;
}
const char* effective_pass() {
  return neon_config().wifi_ssid[0] != '\0' ? neon_config().wifi_pass
                                            : CONFIG_NEON_WIFI_PASSWORD;
}

const char* reason_name(uint8_t r) {
  switch (r) {
    case WIFI_REASON_AUTH_EXPIRE:
      return "auth_expire";
    case WIFI_REASON_AUTH_FAIL:
      return "auth_fail";
    case WIFI_REASON_ASSOC_FAIL:
      return "assoc_fail";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "handshake_timeout";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return "4way_handshake_timeout(wrong_password?)";
    case WIFI_REASON_NO_AP_FOUND:
      return "no_ap_found";
    case WIFI_REASON_BEACON_TIMEOUT:
      return "beacon_timeout";
    case WIFI_REASON_CONNECTION_FAIL:
      return "connection_fail";
    case WIFI_REASON_AUTH_LEAVE:
      return "auth_leave";
    default:
      return "other";
  }
}

void on_wifi_event(void*, esp_event_base_t base, int32_t id, void* event_data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    if (!g_connect_requested) {
      g_connect_requested = true;
      esp_wifi_connect();
    }
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    auto* disc = static_cast<wifi_event_sta_disconnected_t*>(event_data);
    g_last_disconnect_reason = disc != nullptr ? disc->reason : 0;
    g_connect_requested = false;
    if (g_events != nullptr) {
      xEventGroupClearBits(g_events, kGotIpBit);
    }
    netman::preference().wifi_ip(false);
    ESP_LOGW(kTag, "disconnected reason=%u (%s) ssid=\"%s\" — retrying",
             static_cast<unsigned>(g_last_disconnect_reason),
             reason_name(g_last_disconnect_reason), effective_ssid());
    vTaskDelay(pdMS_TO_TICKS(500));
    g_connect_requested = true;
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);
    g_last_disconnect_reason = 0;
    ESP_LOGI(kTag, "got IP " IPSTR " — http://neon-link.local/ or http://" IPSTR
                   "/",
             IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.ip));
    netman::preference().wifi_ip(true);
    if (g_events != nullptr) {
      xEventGroupSetBits(g_events, kGotIpBit);
    }
  }
}

void ensure_events() {
  if (g_events == nullptr) {
    g_events = xEventGroupCreate();
  }
}

void ensure_handlers() {
  if (g_handlers_registered) {
    return;
  }
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &on_wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &on_wifi_event, nullptr));
  g_handlers_registered = true;
}

void fill_sta_config(wifi_config_t* cfg) {
  std::memset(cfg, 0, sizeof(*cfg));
  std::strncpy(reinterpret_cast<char*>(cfg->sta.ssid), effective_ssid(),
               sizeof(cfg->sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(cfg->sta.password), effective_pass(),
               sizeof(cfg->sta.password) - 1);
  // Accept open through WPA2/WPA3-transition. Do not force WPA2-only.
  cfg->sta.threshold.authmode = WIFI_AUTH_OPEN;
  cfg->sta.pmf_cfg.capable = true;
  cfg->sta.pmf_cfg.required = false;
  cfg->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  cfg->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg->sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
}

bool ensure_wifi_driver() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) == ESP_OK) {
    return true;
  }
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  return esp_wifi_init(&init) == ESP_OK;
}

}  // namespace

bool neon_wifi_has_credentials() { return effective_ssid()[0] != '\0'; }

void neon_wifi_start() {
  if (!neon_wifi_has_credentials()) {
    ESP_LOGW(kTag, "no WiFi credentials configured; skipping WiFi");
    return;
  }
  ensure_events();
  netman::init_common();
  if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == nullptr) {
    esp_netif_create_default_wifi_sta();
  }
  if (!ensure_wifi_driver()) {
    ESP_LOGE(kTag, "esp_wifi_init failed");
    return;
  }
  ensure_handlers();

  wifi_config_t cfg = {};
  fill_sta_config(&cfg);

  const wifi_mode_t mode =
      netman::ap_is_up() ? WIFI_MODE_APSTA : WIFI_MODE_STA;
  ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  const esp_err_t start_err = esp_wifi_start();
  if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_NOT_INIT &&
      start_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "esp_wifi_start: %s", esp_err_to_name(start_err));
  }
  g_connect_requested = true;
  esp_wifi_connect();
  g_sta_started = true;
  ESP_LOGI(kTag, "connecting to \"%s\" (pass_len=%u) — ESP32 is 2.4 GHz only",
           effective_ssid(),
           static_cast<unsigned>(std::strlen(effective_pass())));
}

extern "C" void neon_wifi_apply_credentials(void) {
  netman::preference().wifi_configured(neon_wifi_has_credentials());
  if (!neon_wifi_has_credentials()) {
    ESP_LOGW(kTag, "apply: no credentials");
    return;
  }

  if (!g_sta_started) {
    neon_wifi_start();
    return;
  }

  ensure_events();
  wifi_config_t cfg = {};
  fill_sta_config(&cfg);
  if (netman::ap_is_up()) {
    esp_wifi_set_mode(WIFI_MODE_APSTA);
  }
  if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
    ESP_LOGW(kTag, "set_config failed");
    return;
  }
  g_connect_requested = false;
  esp_wifi_disconnect();
  g_connect_requested = true;
  esp_wifi_connect();
  ESP_LOGI(kTag, "reconnecting to \"%s\" (pass_len=%u)", effective_ssid(),
           static_cast<unsigned>(std::strlen(effective_pass())));
}

bool neon_wifi_wait_ip(uint32_t timeout_ms) {
  if (g_events == nullptr) {
    return false;
  }
  const EventBits_t bits = xEventGroupWaitBits(
      g_events, kGotIpBit, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
  return (bits & kGotIpBit) != 0;
}

extern "C" uint8_t neon_wifi_last_disconnect_reason(void) {
  return g_last_disconnect_reason;
}
