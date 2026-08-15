#pragma once

#include <cstdint>

// The session/timeline service: single owner of the timeline session
// (internal on this hardware — no network, no Link; docs/DAISY.md §1).
// Drains the control queue (transport latch, tap, nudge/double/half,
// resync), follows CLK/RST IN through the portable ExtClockEstimator —
// the only external sync this target has — and publishes
// TimelineSnapshots on the timeline bus. Ported from
// teensy41/src/link_service_t41.cpp minus the Link runtime pump.
namespace linksvc {

void init(int64_t now_us);
void poll(int64_t now_us);

// Local transport view (what Toggle flips against).
bool playing();

}  // namespace linksvc
