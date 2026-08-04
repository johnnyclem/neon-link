#pragma once

#include <cstdint>

namespace neon {

// Per-clock-output rate and pulse shape (SOFTWARE.md §3: per-output
// PPQN / division / multiplication, trigger length or square duty,
// shuffle). The effective rate in pulses-per-beat is the rational
//   p / q  =  (ppqn * mult) / div
// so a 4 PPQN output with div=3 emits 4 pulses every 3 beats, exactly.
struct ClockOutputConfig {
  uint32_t ppqn = 4;  // base pulses per quarter note (1..192)
  uint32_t mult = 1;  // multiplier (1..16)
  uint32_t div = 1;   // divider (1..16)

  enum class PulseMode : uint8_t {
    kTrigger = 0,  // fixed-length pulse
    kSquare = 1,   // duty-cycle square
  };
  PulseMode mode = PulseMode::kTrigger;

  uint32_t trig_len_us = 5000;  // kTrigger: pulse width (clamped in-engine)
  uint8_t duty_pct = 50;        // kSquare: high fraction of the period (1..99)

  // Swing: odd-numbered pulses are delayed by this percentage of the pulse
  // period (0..75). Applied to the absolute grid, so parity is stable
  // across re-anchors.
  uint8_t shuffle_pct = 0;

  bool enabled = true;
};

enum class ResetMode : uint8_t {
  kStartOfPlay = 0,  // one pulse when the transport starts (HARDWARE.md
                     // "Reset / Start pulse" — the default)
  kEveryBar = 1,     // pulse at every quantum boundary while playing
  kOff = 2,
};

}  // namespace neon
