#pragma once

#include <cstddef>
#include <cstdint>

// Native Ethernet (QNEthernet): DHCP with a static fallback, mDNS
// (<device-name>.local), and link/address status for the UI and the
// status endpoint. All non-blocking; call poll() every loop.
namespace net {

void init(const char* hostname);
void poll(int64_t now_us);

// Follows a live device_name change (the editor's rename flow).
void set_hostname(const char* hostname);

bool link_up();
bool has_ip();
// Dotted-quad primary IPv4, empty string when none.
void primary_ip(char* out, size_t cap);

}  // namespace net
