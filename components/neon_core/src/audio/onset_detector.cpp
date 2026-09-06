#include "neon/audio/onset_detector.hpp"

#include <cmath>

namespace neon {

void OnsetDetector::reset(uint32_t sample_rate) {
  const float rate = static_cast<float>(sample_rate != 0 ? sample_rate : 44100);
  env_decay_ = std::exp(-1.f / (kEnvTauMs * 0.001f * rate));
  slow_alpha_ = 1.f - std::exp(-1.f / (kSlowTauMs * 0.001f * rate));
  env_ = 0.f;
  slow_ = 0.f;
  prev_ = 0.f;
  last_onset_us_ = INT64_MIN;
  have_onset_ = false;
  n_clicks_ = 0;
}

void OnsetDetector::note_click(int64_t click_us) {
  if (n_clicks_ == kClickGuardCap) {
    for (uint32_t i = 1; i < kClickGuardCap; ++i) {
      clicks_[i - 1] = clicks_[i];
    }
    --n_clicks_;
  }
  clicks_[n_clicks_++] = click_us;
}

void OnsetDetector::expire_click_guards(int64_t t0_us) {
  const int64_t cutoff = t0_us - kClickGuardUs;
  uint32_t w = 0;
  for (uint32_t i = 0; i < n_clicks_; ++i) {
    if (clicks_[i] >= cutoff) {
      clicks_[w++] = clicks_[i];
    }
  }
  n_clicks_ = w;
}

bool OnsetDetector::near_click_guard(int64_t t_us) const {
  for (uint32_t i = 0; i < n_clicks_; ++i) {
    int64_t d = t_us - clicks_[i];
    if (d < 0) {
      d = -d;
    }
    if (d <= kClickGuardUs) {
      return true;
    }
  }
  return false;
}

uint32_t OnsetDetector::process(const float* L, const float* R, uint32_t n,
                                int64_t t0_us, uint64_t us_per_frame_q32,
                                OnsetEvent* out, uint32_t cap) {
  expire_click_guards(t0_us);
  uint32_t k = 0;
  const float scale = 2.0f - (static_cast<float>(sensitivity_) * (1.65f / 255.0f));
  for (uint32_t i = 0; i < n; ++i) {
    const float abs_l = std::fabs(L[i]);
    const float abs_r = std::fabs(R[i]);
    const float x = abs_l > abs_r ? abs_l : abs_r;
    env_ = (x > env_) ? x : env_ * env_decay_;
    slow_ += (env_ - slow_) * slow_alpha_;
    const float flux = env_ - prev_;
    prev_ = env_;

    const float scaled = slow_ * scale;
    const float thresh = (scaled > kThreshFloor) ? scaled : kThreshFloor;
    if (flux <= thresh * kFluxArm) {
      continue;
    }

    const int64_t t_us = t0_us + static_cast<int64_t>(
        (static_cast<uint64_t>(i) * us_per_frame_q32) >> 32);
    if (have_onset_ && t_us >= last_onset_us_ &&
        t_us - last_onset_us_ < kRefractoryUs) {
      continue;
    }
    if (near_click_guard(t_us)) {
      continue;
    }

    last_onset_us_ = t_us;
    have_onset_ = true;
    if (k < cap && out != nullptr) {
      out[k++] = OnsetEvent{t_us, flux};
    }
  }
  return k;
}

}  // namespace neon
