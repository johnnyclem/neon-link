#pragma once

#include "neon/net/preference.hpp"

namespace netman {

// One-time esp_netif + default event loop init, tolerant of either
// already existing. Call before any interface bring-up.
void init_common();

// Bring up the W5500 SPI Ethernet interface (SPI2, pins in
// main/board_pins.h) with route priority above WiFi STA, so lwIP —
// and therefore Ableton Link — prefers the cable whenever it has an
// address. Returns false when the chip is absent or the driver fails;
// the module then runs WiFi-only (normal for bench setups without the
// W5500 wired).
bool ethernet_start();

// Advertise neon-link.local via mDNS (call once after init_common()).
void mdns_start();

// Preference state fed by the ETH/WiFi/IP event handlers.
neon::NetPreference& preference();

}  // namespace netman
