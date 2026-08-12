#pragma once

#include <cstdint>

// True if stored config or menuconfig SSID is non-empty.
bool neon_wifi_has_credentials();

// Bring up STA mode and start connecting (async, with retries).
// No-op without credentials. Safe with setup AP already running (uses APSTA).
void neon_wifi_start();

// Apply credentials from the current config: first-time STA start, or
// disconnect/reconnect when SSID/password changed from the web editor.
// Safe to call from the HTTP handler after neon_config_apply().
// C linkage so components/web_ui can call it without depending on main/.
#ifdef __cplusplus
extern "C" {
#endif
void neon_wifi_apply_credentials(void);
#ifdef __cplusplus
}
#endif

// Block until an IP is acquired or the timeout elapses.
bool neon_wifi_wait_ip(uint32_t timeout_ms);
