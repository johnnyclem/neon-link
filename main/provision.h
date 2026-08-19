#pragma once

#include <cstdint>

// BLE WiFi provisioning (ESP-IDF provisioning manager + reference apps).
// SoftAP fallback after a timeout is the existing setup AP. No-ops on
// boards that are not the link-sync dongle.

void neon_provision_start();
bool neon_provision_active();
// Returns true once when BLE provision has been open 90 s and SoftAP
// should come up as the fallback.
bool neon_provision_poll(int64_t now_us);
void neon_provision_stop();
