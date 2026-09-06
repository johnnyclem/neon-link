#pragma once

// Time-domain onset detector. `t0_us` is the ADC-domain time of input
// frame 0; `us_per_frame_q32` comes from SampleClock
// (`t0_us + ((i * us_per_frame_q32) >> 32)`).

#include <cstdint>

namespace neon {

struct OnsetEvent {
  int64_t t_us = 0;
  float strength = 0.f;
};

class OnsetDetector {
 public:
  static constexpr float kEnvTauMs = 10.f;
  static constexpr float kSlowTauMs = 200.f;
  static constexpr float kFluxArm = 1.8f;
  static constexpr float kThreshFloor = 0.008f;
  static constexpr int64_t kRefractoryUs = 40000;
  static constexpr int64_t kClickGuardUs = 8000;
  static constexpr uint32_t kClickGuardCap = 4;

  void reset(uint32_t sample_rate);
  void set_sensitivity(uint8_t s) { sensitivity_ = s; }
  uint8_t sensitivity() const { return sensitivity_; }

  // ADC-domain µs; kept until it ages out of the ±8 ms guard window.
  void note_click(int64_t click_us);

  uint32_t process(const float* L, const float* R, uint32_t n, int64_t t0_us,
                   uint64_t us_per_frame_q32, OnsetEvent* out, uint32_t cap);

 private:
  void expire_click_guards(int64_t t0_us);
  bool near_click_guard(int64_t t_us) const;

  uint8_t sensitivity_ = 128;
  float env_decay_ = 0.f;
  float slow_alpha_ = 0.f;
  float env_ = 0.f;
  float slow_ = 0.f;
  float prev_ = 0.f;
  int64_t last_onset_us_ = INT64_MIN;
  bool have_onset_ = false;

  int64_t clicks_[kClickGuardCap] = {};
  uint32_t n_clicks_ = 0;
};

}  // namespace neon
