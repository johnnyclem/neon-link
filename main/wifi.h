#pragma once

#include <cstdint>

// True if any stored network (or the menuconfig SSID) is usable. False
// when the access point policy is "always", which never joins a network.
bool neon_wifi_has_credentials();

// Bring up STA mode and start connecting (async, with retries). Walks the
// stored network list, giving each `wifi_retries` attempts before moving
// on. No-op without credentials. Safe with the setup AP running (APSTA).
void neon_wifi_start();

// Stop station association and retries so a setup AP can beacon on a
// fixed channel. SoftAP disappears from phones if STA keeps scanning.
// neon_wifi_start / neon_wifi_apply_credentials resume joining.
void neon_wifi_hold_station();

// Apply credentials from the current config: first-time STA start, or
// disconnect/reconnect when the stored list changed from the web editor.
// Safe to call from the HTTP handler after neon_config_apply().
// C linkage so components/web_ui can call it without depending on main/.
#ifdef __cplusplus
extern "C" {
#endif
void neon_wifi_apply_credentials(void);
uint8_t neon_wifi_last_disconnect_reason(void);
// SSID of the network currently being attempted (never null).
const char* neon_wifi_current_ssid(void);
// Blocking scan rendered straight to a JSON array, so components/web_ui
// can offer a pick-list without depending on main/. Returns bytes written
// (0 on failure); always emits a valid array, possibly empty.
int neon_wifi_scan_json(char* buf, int cap);
#ifdef __cplusplus
}
#endif

// Block until an IP is acquired or the timeout elapses.
bool neon_wifi_wait_ip(uint32_t timeout_ms);

// One access point seen by neon_wifi_scan().
struct NeonWifiScanEntry {
  char ssid[33];
  int8_t rssi;
  uint8_t open;  // 1 = no password required
};

// Blocking scan for nearby 2.4 GHz networks, so the editor can offer a
// pick-list instead of asking users to type an SSID. Returns the number
// of entries written.
int neon_wifi_scan(NeonWifiScanEntry* out, int max_entries);
