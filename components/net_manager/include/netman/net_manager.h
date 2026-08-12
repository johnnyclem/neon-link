#pragma once

#include <cstddef>
#include <cstdint>

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

// Advertise <hostname>.local via mDNS (call once after init_common()).
// Registers hostname + _http._tcp so browsers/OS can find the editor.
// `hostname` comes from Config::device_name and is already DNS-safe.
void mdns_start(const char* hostname);

// Update the advertised hostname after the user renames the module, so
// the editor URL follows without a reboot.
void mdns_set_hostname(const char* hostname);

// Access point parameters, derived from the stored config.
struct ApParams {
  const char* ssid;  // never null, already resolved (device name + MAC)
  const char* pass;  // ignored when require_pass is false
  bool require_pass;
  bool hidden;
  uint8_t channel;
};

// Setup access point at 192.168.4.1, serving the web editor for
// configuration and acting as a Link network of its own. Keeps STA
// running if it was started. Idempotent — a second call with different
// parameters reconfigures the AP in place.
bool ap_start(const ApParams& params);

// True after a successful ap_start().
bool ap_is_up();

// The SSID the access point is currently advertising ("" when down).
const char* ap_ssid();

// Best reachable IPv4 for the editor, preference order:
// Ethernet → WiFi STA → setup AP (192.168.4.1). Writes "a.b.c.d" into
// buf; returns false and writes "" when nothing has an address yet.
bool primary_ip(char* buf, size_t len);

// Preference state fed by the ETH/WiFi/IP event handlers.
neon::NetPreference& preference();

}  // namespace netman
