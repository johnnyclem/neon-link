#pragma once

// Single-LED status vocabulary for the link-sync dongle. Portable so the
// patterns can be unit-tested without GPIO. Duty is 0 (off) .. 255 (full);
// the HAL applies the XIAO's inverted LED wiring.

#include <cstdint>

#include "neon/timeline.hpp"

namespace neon {

enum class LedPattern : uint8_t {
  Unprovisioned = 0,  // breathe, 2 s period
  Connecting = 1,     // 5 Hz blink
  LinkNoPeers = 2,    // double-blink every 2 s
  LinkStopped = 3,    // solid
  Playing = 4,        // flash on the downbeat only
};

// Highest-priority pattern that matches the current net / session state.
LedPattern classify_led(bool provisioned, bool wifi_up, uint32_t peers,
                        bool playing);

// Instantaneous brightness. `tl` is only read for Playing (downbeat).
uint8_t led_duty(LedPattern pattern, const TimelineSnapshot& tl,
                 int64_t now_us);

}  // namespace neon
