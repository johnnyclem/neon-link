#pragma once

#include "hal/IPulseHw.hpp"

// hal::IPulseHw on an IntervalTimer (PIT): a 100 µs tick drains a
// single-producer/single-consumer edge ring and drives the six pulse
// pins with digitalWriteFast. Worst-case edge placement error is one
// tick — fine for bring-up; the sample-accurate follow-up is a FlexPWM
// one-shot per channel (docs/TEENSY41.md §7).
//
// Masks in submitted edges use virtual channel bits 0..5 (CLK1..4,
// RESET, RUN), mapped to real pins through kPulsePins — the same virtual
// scheme as the AMYboard build.
class PulseHwT41 : public hal::IPulseHw {
 public:
  // Configures the output pins low and starts the timer. One instance only.
  bool init();

  bool submit(const hal::PulseEdge& e) override;
  int64_t now_us() const override;

  // Current output level word (bits 0..5), for LED mirroring and the UI.
  static uint32_t levels();

  // Diagnostics for /api/status ("pulse" block, same fields as the ESP
  // GPTimer path reports): total edges emitted, and how late the ISR was
  // relative to each edge's scheduled time.
  static uint32_t edges();
  static uint32_t late_max_us();
  static uint32_t late_avg_us();

  // Edges fired more than one tick late (subset of the above).
  static uint32_t late_edges();
};
