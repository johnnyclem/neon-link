#pragma once

#include <cstddef>
#include <cstdint>

#include "neon/net/preference.hpp"

namespace netman {

// One-time esp_netif + default event loop init, tolerant of either
// already existing. Call before any interface bring-up.
void init_common();

// One-shot esp_wifi_init(). On ESP-Hosted (P4+C6) this is what resets
// the slave and waits for SDIO. Do not probe with esp_wifi_get_mode()
// first — wifi_remote turns that into an RPC while transport is still
// down, and Hosted then double-frees. Returns false if the driver
// (or the C6) does not come up. Concurrent callers return false
// immediately rather than stacking a second blocking init.
bool wifi_driver_init();

// True after a successful wifi_driver_init(). Does not block.
bool wifi_driver_ready();

// Bring up the W5500 SPI Ethernet interface (SPI2, pins in
// main/board_pins.h) with route priority above WiFi STA, so lwIP —
// and therefore Ableton Link — prefers the cable whenever it has an
// address. Returns false when the chip is absent or the driver fails;
// the module then runs WiFi-only (normal for bench setups without the
// W5500 wired).
bool ethernet_start();

// Advertise <hostname>.local via mDNS (call once after init_common()).
// Registers hostname + _http._tcp so browsers/OS can find the editor.
// Instance name is the DNS-safe device_name (unique per renamed unit).
// TXT: path=/, fw=<app version>, name=<device_name>, id=<last 3 MAC bytes>.
// `hostname` comes from Config::device_name and is already DNS-safe.
void mdns_start(const char* hostname);

// Update the advertised hostname, instance name, and TXT `name` after
// the user renames the module, so the editor URL follows without a reboot.
void mdns_set_hostname(const char* hostname);

// Access point parameters, derived from the stored config.
struct ApParams {
  const char* ssid;  // never null, already resolved (device name + MAC)
  const char* pass;  // ignored when require_pass is false
  bool require_pass;
  bool hidden;
  uint8_t channel;
  // When false (the setup-AP case), drop station mode so SoftAP beacons
  // stay on one channel. APSTA + a scanning STA makes the network vanish.
  bool keep_sta;
};

// Setup access point at 192.168.4.1, serving the web editor for
// configuration and acting as a Link network of its own. Keeps STA
// running if it was started. Idempotent — a second call with different
// parameters reconfigures the AP in place.
bool ap_start(const ApParams& params);

// Drop the setup AP. STA mode is left to the caller (typically
// neon_wifi_start after the user saved studio credentials). Idempotent.
bool ap_stop();

// True after a successful ap_start().
bool ap_is_up();

// C3 Super Mini RF diagnostic. TX cap in 0.25 dBm units (34 = 8.5 dBm),
// or 0 if unset. Nearby AP count from the boot listen probe, or -1.
int8_t wifi_tx_qdBm();
int wifi_nearby_count();

// Board-specific RF limits (C3 Super Mini TX cap). Call after every
// successful esp_wifi_start().
void wifi_after_start();

// The SSID the access point is currently advertising ("" when down).
const char* ap_ssid();

// Best reachable IPv4 for the editor, preference order:
// Ethernet → WiFi STA → setup AP (192.168.4.1). Writes "a.b.c.d" into
// buf; returns false and writes "" when nothing has an address yet.
bool primary_ip(char* buf, size_t len);

// Preference state fed by the ETH/WiFi/IP event handlers.
neon::NetPreference& preference();

}  // namespace netman
