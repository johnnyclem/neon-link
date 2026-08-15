#pragma once

#include "hal/IPulseHw.hpp"

// hal::IPulseHw on TIM5, free-running at 1 MHz with two compare
// channels:
//
//   CC1 — the precise shot: re-armed to the exact microsecond of the
//     next pending edge (when it is due inside the arming window), so
//     edge placement error is interrupt latency (~1-2 µs at priority 4,
//     above MIDI and the audio DMA) instead of the polled tick's
//     ±100 µs. This is the follow-up promised by docs/DAISY.md §7.3.
//   CC2 — the 10 kHz housekeeping tick the old design was built on: it
//     services the shared timebase wrap extender, samples the encoders
//     and CLK/RST IN (see neon_daisy_input_sample_isr in the mains),
//     drains any edge the precise shot could not have known about, and
//     arms CC1 for whatever falls due in its next window. The engine's
//     ≥2 ms scheduling lead guarantees every edge is seen by a tick
//     long before it is due, so the producer never touches the timer.
//
// Masks in submitted edges use virtual channel bits 0..5 (CLK1..4,
// RESET, RUN); kPulsePinChannel maps each physical jack to its bit, so
// boards with fewer jacks keep full engine semantics.
class PulseHwDaisy : public hal::IPulseHw {
 public:
  // Configures the output pins low and starts the timer. One instance
  // only. Call after DaisySeed/DaisyPatchSM Init().
  bool init();

  bool submit(const hal::PulseEdge& e) override;
  int64_t now_us() const override;

  // Current output level word (bits 0..5), for LED mirroring and the UI.
  static uint32_t levels();

  // MIDI note gates: set a channel's level immediately, bypassing the
  // scheduled edge stream (an edge injected into the ordered ring would
  // wait behind everything already scheduled). Point gates at an output
  // whose role is not kClock so the two sources don't fight — the same
  // contract as the ESP pulse task. IRQ-masked; callable from the main
  // loop.
  static void set_level_now(uint8_t channel, bool on);

  // Diagnostics (same fields the ESP GPTimer and Teensy paths report):
  // total edges emitted, and how late emission was relative to each
  // edge's scheduled time.
  static uint32_t edges();
  static uint32_t late_max_us();
  static uint32_t late_avg_us();

  // Edges emitted more than 10 µs late — with the compare-interrupt
  // emitter this should stay at zero on a healthy build (the polled
  // predecessor counted >100 µs here).
  static uint32_t late_edges();
};

// Implemented in the mains: sampling hook the 100 µs housekeeping tick
// calls for the encoders and CLK/RST IN capture.
void neon_daisy_input_sample_isr(int64_t now_us);
