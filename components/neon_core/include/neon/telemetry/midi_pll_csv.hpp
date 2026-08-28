#pragma once

// Per-tick CSV telemetry for the MIDI clock PLL
// (docs/MIDI_PLL_PHASES_HANDOFF.md Phase E): residual, tempo, and lock
// state for every 0xF8, so Phase A's bench acceptance is measurable
// without a scope and future loop regressions show up in a diffable
// trace. Same portable formatter idiom as csv.hpp/linksync_csv.hpp; the
// service layer prints "PLL,<line>" on the console UART (a distinct tag
// from the 1 Hz "TEL," stream so both can share the port —
// tools/studio_mode/uart_telemetry_logger.py --prefix PLL captures it).

#include <cstddef>
#include <cstdint>

#include "neon/midi/clock_pll.hpp"

namespace neon {

struct MidiPllTelemetrySample {
  int64_t t_us = 0;         // this tick's raw timestamp, shared µs timebase
  uint32_t tick = 0;        // ticks received since the stream (re)started
  const char* transport = "din";  // "din" | "usb" | "ble". Never null.
  int64_t residual_us = 0;  // prediction error of this tick, pre-update
  uint32_t tempo_milli_bpm = 0;  // 0 until the rate is seeded
  uint8_t locked = 0;
  uint8_t playing = 0;
  uint8_t beat_valid = 0;
  uint8_t following = 0;  // arbiter verdict: the session follows this PLL
};

const char* midi_pll_transport_str(MidiClockPll::Transport t);

// Snapshot the PLL after an on_tick. `t_us` is that tick's timestamp (the
// PLL keeps only filtered state); `following` comes from the caller
// because arbitration lives above the PLL.
MidiPllTelemetrySample midi_pll_telemetry_sample(const MidiClockPll& pll,
                                                 int64_t t_us, bool following);

// Column header matching midi_pll_telemetry_csv_line()'s field order, no
// trailing newline. Returns the length written, or 0 if `cap` is too small.
size_t midi_pll_telemetry_csv_header(char* out, size_t cap);

// One data line, no trailing newline (the caller appends "\n").
// Returns the length written, or 0 if `cap` is too small.
size_t midi_pll_telemetry_csv_line(const MidiPllTelemetrySample& s, char* out,
                                   size_t cap);

}  // namespace neon
