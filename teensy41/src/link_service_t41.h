#pragma once

#include <cstdint>

// The Link service, polled from loop(): owns the session, drains the
// control queue (encoders, touch, web editor), follows CLK/RST IN when
// external clock is active, and publishes integer timeline snapshots to
// the pulse engine and the UI. A direct port of main/link_service.cpp's
// task body minus WiFi/AP management.
namespace linksvc {

void init(int64_t now_us);
void poll(int64_t now_us);

// Current local transport view (kept in step with peers), for the UI.
bool playing();

}  // namespace linksvc
