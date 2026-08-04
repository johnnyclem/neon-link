#pragma once

#include <cstddef>
#include <cstdint>

namespace neon {

// Tempo is carried through the engine as microseconds-per-beat in Q32.32
// fixed point. Doubles are only permitted at the Link boundary (core 0);
// everything reachable from the pulse path is integer math.
//
// milli_bpm is BPM * 1000 (e.g. 120000 = 120 BPM). Values below 1000
// (1 BPM) are clamped so the intermediate math cannot overflow.
uint64_t micros_per_beat_q32_from_milli_bpm(uint32_t milli_bpm);

struct Edge {
  int64_t t_us;     // absolute time in the engine's timebase
  uint8_t channel;  // output index (v0: always 0)
  bool high;        // rising (true) or falling (false)
};

struct OutputSettings {
  uint32_t ppqn = 4;            // pulses per quarter note
  uint32_t trig_len_us = 5000;  // trigger pulse width; clamped below one period
};

// v0 edge scheduler: one output, fixed tempo, drift-free integer beat grid.
//
// The tick period mpb/ppqn is accumulated as (quotient, remainder) so that
// exactly every `ppqn` ticks the accumulated time equals mpb_q32 with zero
// rounding loss: the grid never drifts from the ideal beat grid regardless
// of runtime.
//
// Callers pull edges with generate() over contiguous half-open windows
// [t0, t1). Edges scheduled before t0 are consumed silently, so windows
// must be contiguous to avoid dropping pulses.
class ClockEngine {
 public:
  // Restart the grid: the first rising edge lands exactly at origin_us.
  void reset(int64_t origin_us);

  void set_tempo(uint64_t micros_per_beat_q32);
  void set_output(const OutputSettings& s);

  // Append up to max_out edges with t_us in [t0_us, t1_us), in
  // non-decreasing time order. Returns the number written. If the return
  // value equals max_out there may be more edges in the window; call again
  // with the same window to continue.
  size_t generate(int64_t t0_us, int64_t t1_us, Edge* out, size_t max_out);

 private:
  void recompute_period();
  void advance_rise();
  int64_t effective_trig_len() const;

  bool running_ = false;
  uint64_t mpb_q32_ = 0;

  // Tick period as quotient/remainder of mpb_q32 / ppqn (see class comment).
  uint64_t period_q32_ = 0;
  uint32_t period_rem_ = 0;
  uint32_t rem_acc_ = 0;

  OutputSettings out_{};

  // Next rising edge: integer microseconds plus 32-bit fractional part.
  int64_t rise_us_ = 0;
  uint32_t rise_frac_ = 0;

  static constexpr int64_t kNoFall = INT64_MIN;
  int64_t fall_us_ = kNoFall;
};

}  // namespace neon
