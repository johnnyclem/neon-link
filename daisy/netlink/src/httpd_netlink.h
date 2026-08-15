#pragma once

#include <cstdint>

// The web editor served from the module (httpd_t41.cpp's server,
// re-plumbed onto lwIP's raw TCP API): the committed gzipped
// single-file app at GET /, and the same /api/* REST surface the
// editor and the VST plugin speak. One request is serviced at a time,
// non-blocking, so a slow client can never stall the loop that feeds
// the pulse engine.
//
// Differences from the ESP server, all reported honestly in JSON:
//   /api/scan            -> []      (no WiFi radio to scan with)
//   /api/audio/channels  -> {"available":false}  (Link Audio not ported)
//   /api/ota             -> 501     (flash over USB with the Daisy
//                                    bootloader's DFU mode)
namespace webui {

void init();
void poll(int64_t now_us);

// True once /api/reboot or /api/factory_reset asked for a restart; the
// main loop performs it after the response drains.
bool reboot_requested();

}  // namespace webui
