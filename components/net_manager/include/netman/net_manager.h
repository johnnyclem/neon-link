#pragma once

#include <cstddef>

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
// Registers hostname + _http._tcp so browsers/OS can find the editor.
void mdns_start();

// Setup access point: open network "NEON-LINK-XXXX" (XXXX from the MAC)
// at 192.168.4.1, serving the web editor for first-time configuration.
// Keeps STA running if it was started. Idempotent.
bool ap_start();

// True after a successful ap_start().
bool ap_is_up();

// Best reachable IPv4 for the editor, preference order:
// Ethernet → WiFi STA → setup AP (192.168.4.1). Writes "a.b.c.d" into
// buf; returns false and writes "" when nothing has an address yet.
bool primary_ip(char* buf, size_t len);

// Preference state fed by the ETH/WiFi/IP event handlers.
neon::NetPreference& preference();

}  // namespace netman
