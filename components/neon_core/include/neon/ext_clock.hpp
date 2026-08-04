#pragma once

#include <cstdint>

namespace neon {

// External clock tempo estimator (SOFTWARE.md §5 "Bidirectional Logic"):
// measures the period of pulses on CLK IN and produces a stable tempo the
// service layer forwards to Link's setTempo, plus phase-anchor requests
// from RST IN. Pure logic — the ISR layer feeds raw microsecond
// timestamps; host tests drive synthetic streams.
//
// Pipeline per pulse:
//   inter-pulse period -> outlier rejection (0.5x..2x the current median
//   guards against bounce and dropouts) -> median-of-5 -> EMA (alpha 1/8)
//   -> tempo in milli-BPM.
//
// Publishing is hysteretic: a new tempo fires only when the smoothed
// estimate moves more than 0.5% from the last published value, and then
// only after the estimate has settled (kSettlePulses in the band around
// the candidate) — robust following without setTempo spam that would
// fight the session.
class ExtClockEstimator {
 public:
  static constexpr uint32_t kMedianWindow = 5;
  static constexpr uint32_t kSettlePulses = 3;
  static constexpr int64_t kMinTimeoutUs = 2000000;
  // Publish threshold: 0.5% expressed as 1/200.
  static constexpr int64_t kHysteresisDen = 200;

  // pulses per quarter note arriving on CLK IN (1..96).
  void set_input_ppqn(uint32_t ppqn);
  uint32_t input_ppqn() const { return ppqn_; }

  void on_pulse(int64_t t_us);
  void on_reset(int64_t t_us);

  // True while pulses are arriving (no gap beyond 4x the expected period,
  // min 2 s). Going inactive clears the estimate history.
  bool active(int64_t now_us);

  // One-shot: true when a materially new tempo is available.
  bool take_tempo_update(uint32_t* milli_bpm);

  // One-shot: true when RST IN requested a phase anchor ("the downbeat
  // was at *t_us").
  bool take_phase_request(int64_t* t_us);

  // Current smoothed estimate (0 until enough pulses).
  uint32_t tempo_milli_bpm() const { return published_mbpm_; }

 private:
  void reset_history();
  int64_t median_period() const;

  uint32_t ppqn_ = 4;

  int64_t last_pulse_us_ = INT64_MIN;
  int64_t periods_[kMedianWindow] = {};
  uint32_t period_count_ = 0;
  uint32_t period_next_ = 0;
  uint32_t long_rejects_ = 0;

  int64_t ema_period_q8_ = 0;  // period in µs, Q(56).8 fixed point

  uint32_t published_mbpm_ = 0;
  uint32_t candidate_mbpm_ = 0;
  uint32_t candidate_streak_ = 0;
  bool update_pending_ = false;
  uint32_t pending_mbpm_ = 0;

  bool phase_pending_ = false;
  int64_t phase_t_us_ = 0;
};

}  // namespace neon
