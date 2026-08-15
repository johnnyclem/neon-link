#pragma once

// The portable half of P4's UART CSV telemetry (docs/STUDIO_MODE_TEST_PLAN.md,
// neon/telemetry/csv.hpp): deciding *when* to emit a header/data line, what
// mode string to report, and how to turn an AudioStatus snapshot into a
// TelemetrySample. Pulled out of main/audio_service.cpp so this logic is
// host-testable — it was originally two loop-scoped counters and an inline
// field-by-field copy in an ESP-only task, exactly the kind of easy-to-get-
// subtly-wrong code (off-by-one on the cadence, a mismapped field) that is
// worth verifying directly rather than by inspection. The ESP-only glue
// (main/audio_service.cpp) now just calls these each control-loop tick and
// writes the result to the console UART.

#include <cstddef>
#include <cstdint>

#include "neon/audio/types.hpp"
#include "neon/telemetry/csv.hpp"

namespace neon {

// "sta" | "ap" | "apsta" | "eth" | "none". Ethernet wins outright (mirrors
// neon::NetPreference::active()'s own priority); AP and STA are independent
// and can both be true (APSTA).
const char* telemetry_mode_str(bool eth_active, bool ap_up, bool sta_up);

// Maps a render-task AudioStatus snapshot onto the flat TelemetrySample the
// CSV formatter wants. `mode` and `uptime_ms` come from the caller because
// neither lives on AudioStatus (mode is netman state; uptime needs a clock
// this module deliberately does not depend on).
TelemetrySample telemetry_sample_from_status(const AudioStatus& status,
                                             const char* mode,
                                             uint64_t uptime_ms);

// One line per what the caller should do this control-loop tick.
struct TelemetryTick {
  bool want_header = false;
  bool want_line = false;
};

// Edge-triggered header + throttled-to-1-line-per-N-ticks cadence, driven by
// a bool "is telemetry enabled" sampled once per control-loop tick (the
// audio_ctl_task loop, ~250 ms). A header is wanted exactly on the
// disabled->enabled transition (so re-enabling reprints it), and the first
// data line lands on that same tick rather than waiting out a stale
// countdown from before the last disable.
class TelemetryTicker {
 public:
  // ticks_per_line: how many tick() calls make up one emitted data line
  // (e.g. 4 for a 250 ms loop emitting at 1 Hz). 0 is treated as 1 (emit
  // every tick) rather than dividing by zero.
  explicit TelemetryTicker(uint32_t ticks_per_line)
      : ticks_per_line_(ticks_per_line != 0 ? ticks_per_line : 1) {}

  TelemetryTick tick(bool enabled);

 private:
  uint32_t ticks_per_line_;
  bool was_enabled_ = false;
  uint32_t countdown_ = 0;
};

}  // namespace neon
