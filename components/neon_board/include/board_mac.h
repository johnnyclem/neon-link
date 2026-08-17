#pragma once

// Stable per-unit MAC. WiFi-typed reads (STA / SoftAP) fail on chips
// that have no radio (ESP32-P4). Factory eFuse is the same number the
// USB descriptor already printed.

#include "esp_mac.h"
#include "sdkconfig.h"

#include <cstring>

inline void neon_read_unit_mac(uint8_t mac[6]) {
  std::memset(mac, 0, 6);
#if CONFIG_IDF_TARGET_ESP32P4
  if (esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY) != ESP_OK) {
    (void)esp_read_mac(mac, ESP_MAC_BASE);
  }
#else
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    (void)esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
  }
#endif
}
