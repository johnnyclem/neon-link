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

EventGroupHandle_t g_events;

void on_wifi_event(void*, esp_event_base_t base, int32_t id, void* event_data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(g_events, kGotIpBit);
    netman::preference().wifi_ip(false);
    ESP_LOGW(kTag, "disconnected, retrying");
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);
    ESP_LOGI(kTag, "got IP " IPSTR " — http://neon-link.local/ or http://" IPSTR
                   "/",
             IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.ip));
    netman::preference().wifi_ip(true);
    xEventGroupSetBits(g_events, kGotIpBit);
  }
}

// Stored config wins; the menuconfig credentials are the fallback for
// development builds.
const char* effective_ssid() {
  return neon_config().wifi_ssid[0] != '\0' ? neon_config().wifi_ssid
                                            : CONFIG_NEON_WIFI_SSID;
}
const char* effective_pass() {
  return neon_config().wifi_ssid[0] != '\0' ? neon_config().wifi_pass
                                            : CONFIG_NEON_WIFI_PASSWORD;
}

}  // namespace

bool neon_wifi_has_credentials() { return effective_ssid()[0] != '\0'; }

void neon_wifi_start() {
  if (!neon_wifi_has_credentials()) {
    ESP_LOGW(kTag, "no WiFi credentials configured; skipping WiFi");
    return;
  }
  g_events = xEventGroupCreate();

  netman::init_common();  // tolerant of prior esp_netif/event-loop init
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &on_wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &on_wifi_event, nullptr));

  wifi_config_t cfg = {};
  std::strncpy(reinterpret_cast<char*>(cfg.sta.ssid), effective_ssid(),
               sizeof(cfg.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(cfg.sta.password), effective_pass(),
               sizeof(cfg.sta.password) - 1);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(kTag, "connecting to \"%s\"", effective_ssid());
}

bool neon_wifi_wait_ip(uint32_t timeout_ms) {
  if (g_events == nullptr) {
    return false;
  }
  const EventBits_t bits = xEventGroupWaitBits(
      g_events, kGotIpBit, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
  return (bits & kGotIpBit) != 0;
}
