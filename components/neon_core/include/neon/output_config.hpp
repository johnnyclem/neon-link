#pragma once

#include <cstdint>

namespace neon {

// What an output jack does. Mirrors (and extends) the mode list every
// legacy Link-to-CV box exposes, but here it is settable per output
// instead of being wired to a fixed jack:
//
//   Clock        pulses while the transport plays (or always, if free_run)
//   Gate         high while the transport plays
//   ResetLoop    trigger at the start of every loop while playing
//   ResetStart   trigger at the start of the first loop when play begins
//   ResetStop    trigger at the end of the final loop when play ends
enum class OutputRole : uint8_t {
  kClock = 0,
  kGate = 1,
  kResetLoop = 2,
  kResetStart = 3,
  kResetStop = 4,
};

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

  // Jack role. Anything other than kClock ignores the rate fields except
  // trig_len_us (used as the reset pulse width).
  OutputRole role = OutputRole::kClock;

  // "Clock (Always On)": keep pulsing even when EngineConfig
  // transport_gating would otherwise mute this output while stopped.
  // Pair with a second output in kGate role for DIN-Sync style clocking.
  bool free_run = false;

  // Rhythm Explorer (milestone 9): pattern selection on top of the grid.
  // Skipped ticks advance the grid silently, so patterns stay locked to
  // the session beat.
  enum class RhythmMode : uint8_t {
    kAll = 0,          // every tick fires (classic clock)
    kEuclid = 1,       // Euclidean E(fills, steps) with rotation
    kProbability = 2,  // every tick is a coin flip at probability_pct
    kPattern = 3,      // free assignment: step_mask selects the live steps
  };
  RhythmMode rhythm = RhythmMode::kAll;
  uint8_t euclid_steps = 16;  // 1..64 — also the kPattern step count
  uint8_t euclid_fills = 16;  // 0..64
  uint8_t euclid_rot = 0;     // 0..63

  // "Chance": probability that an otherwise-enabled step actually fires.
  // Applies to every rhythm mode except kAll (a plain clock is never
  // diced), so Euclidean and free-assignment patterns can both be thinned.
  uint8_t probability_pct = 100;

  // Free-assignment step mask, bit n = step n. Only the low euclid_steps
  // bits are consulted. Defaults to every step on.
  uint64_t step_mask = ~0ull;

  // When set, the rhythm pattern's steps are distributed across one loop
  // (EngineConfig quantum) instead of running on the ppqn/mult/div grid —
  // "16 steps spread across 4 beats". Ignored when rhythm is kAll.
  bool rhythm_over_loop = false;

  // Humanize: deterministic pseudo-random delay of each pulse, up to this
  // percentage of the period (0..50).
  uint8_t humanize_pct = 0;
};

enum class ResetMode : uint8_t {
  kStartOfPlay = 0,  // one pulse when the transport starts (HARDWARE.md
                     // "Reset / Start pulse" — the default)
  kEveryBar = 1,     // pulse at every quantum boundary while playing
  kOff = 2,
  kAtStop = 3,       // one pulse when the transport stops
};

}  // namespace neon
