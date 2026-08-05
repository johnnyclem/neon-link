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

  // Rhythm Explorer (milestone 9): pattern selection on top of the grid.
  // Skipped ticks advance the grid silently, so patterns stay locked to
  // the session beat.
  enum class RhythmMode : uint8_t {
    kAll = 0,          // every tick fires (classic clock)
    kEuclid = 1,       // Euclidean E(fills, steps) with rotation
    kProbability = 2,  // each tick fires with probability_pct
  };
  RhythmMode rhythm = RhythmMode::kAll;
  uint8_t euclid_steps = 16;  // 1..64
  uint8_t euclid_fills = 16;  // 0..64
  uint8_t euclid_rot = 0;     // 0..63
  uint8_t probability_pct = 100;

  // Humanize: deterministic pseudo-random delay of each pulse, up to this
  // percentage of the period (0..50).
  uint8_t humanize_pct = 0;
};

enum class ResetMode : uint8_t {
  kStartOfPlay = 0,  // one pulse when the transport starts (HARDWARE.md
                     // "Reset / Start pulse" — the default)
  kEveryBar = 1,     // pulse at every quantum boundary while playing
  kOff = 2,
};

}  // namespace neon
