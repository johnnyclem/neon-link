#pragma once

#include <cstddef>
#include <cstdint>

// USB gadget networking (net_t41.h's surface, minus real Ethernet): the
// CDC-ECM function from usbd_ecm.c bridged onto an lwIP netif, with
// DHCP + AutoIP link-local fallback and an mDNS responder so
// <device-name>.local resolves once the host bridges or shares its
// connection. All non-blocking; call poll() every loop.
namespace usbnet {

void init(const char* hostname);
void poll(int64_t now_us);

// Follows a live device_name change (the editor's rename flow).
void set_hostname(const char* hostname);

// True once the host has opened the data interface (its "cable
// plugged" moment — alt setting 1 selected).
bool link_up();
bool has_ip();
// Dotted-quad primary IPv4, empty string when none.
void primary_ip(char* out, size_t cap);

}  // namespace usbnet
