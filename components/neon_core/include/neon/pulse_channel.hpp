#pragma once

#include <cstdint>

#include "neon/output_config.hpp"
#include "neon/timeline.hpp"

namespace neon {

struct Edge {
  int64_t t_us;     // absolute time in the engine's timebase
  uint8_t channel;  // output index, assigned by the owner
  bool high;        // rising (true) or falling (false)
};

// One output's edge scheduler on the session beat grid.
//
// The rate is the rational p/q pulses-per-beat from ClockOutputConfig, so
// tick k sits at session beat k*q/p. The tick period mpb*q/p is advanced
// as (quotient, remainder) against denominator p — every p ticks sum to
// exactly q beats of microseconds with zero rounding loss, so the grid
// never drifts from the session grid regardless of runtime. Anchor
// rounding on retime is < 2^-32 beats and does not accumulate.
//
// Shuffle delays odd ticks by shuffle_pct% of the period at emission time;
// the internal grid stays straight, and parity is derived from the
// absolute tick index so swing feel survives re-anchors. A signed latency
// offset shifts every emitted edge uniformly.
//
// Consumption model: peek() exposes the next edge, pop() consumes it.
// The owner merges multiple channels by time.
class PulseChannel {
 public:
  void configure(const ClockOutputConfig& cfg);
  void set_latency(int32_t latency_us);

  // Re-anchor to the session timeline; the next rise is the first tick at
  // or after from_us (pre-latency). A pending falling edge is preserved so
  // an in-flight pulse always completes. `running` gates new rises.
  void retime(const TimelineSnapshot& tl, int64_t from_us, bool running);

  // Next edge with shuffle and latency applied, or false if idle.
  bool peek(Edge* out) const;
  void pop();

 private:
  void recompute_period();
  void advance_tick();
  int64_t shuffled_rise() const;
  int64_t next_shuffled_rise_after_advance();
  int64_t swing_us() const;

  ClockOutputConfig cfg_{};
  int32_t latency_us_ = 0;
  bool running_ = false;

  uint64_t mpb_q32_ = 0;
  uint32_t rate_p_ = 4;  // pulses per q beats
  uint32_t rate_q_ = 1;

  // Tick period = mpb*q/p as quotient (Q32.32) + remainder over p.
  uint64_t period_q32_ = 0;
  uint32_t period_rem_ = 0;
  uint32_t rem_acc_ = 0;

  // Next grid (unshuffled) rise: integer µs + 32-bit fraction, plus the
  // absolute tick index (for shuffle parity).
  int64_t rise_us_ = 0;
  uint32_t rise_frac_ = 0;
  int64_t tick_index_ = 0;

  static constexpr int64_t kNoFall = INT64_MIN;
  int64_t fall_us_ = kNoFall;  // pre-latency absolute time
};

}  // namespace neon
