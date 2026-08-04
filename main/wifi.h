#pragma once

#include <cstdint>

// True if CONFIG_NEON_WIFI_SSID is non-empty.
bool neon_wifi_has_credentials();

// Bring up STA mode and start connecting (async, with retries). Safe to
// call only once. No-op without credentials.
void neon_wifi_start();

// Block until an IP is acquired or the timeout elapses.
bool neon_wifi_wait_ip(uint32_t timeout_ms);
