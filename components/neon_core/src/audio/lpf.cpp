#include "neon/audio/lpf.hpp"

#include <cmath>

namespace neon {

void Biquad::reset() {
  z1_ = 0.0f;
  z2_ = 0.0f;
}

void Biquad::set(BiquadKind kind, uint32_t sample_rate, float cutoff_hz,
                 float q) {
  const float fs = sample_rate != 0 ? static_cast<float>(sample_rate) : 48000.0f;
  float fc = cutoff_hz > 0.0f ? cutoff_hz : kGistHighCutHz;
  // Keep the pole inside the unit circle and well below Nyquist.
  const float nyquist = fs * 0.45f;
  if (fc > nyquist) {
    fc = nyquist;
  }
  const float qq = q > 0.05f ? q : kGistQ;

  const float w0 = 6.28318530718f * fc / fs;
  const float c = cosf(w0);
  const float s = sinf(w0);
  const float alpha = s / (2.0f * qq);
  const float a0 = 1.0f + alpha;
  if (kind == BiquadKind::kHighpass) {
    b0_ = ((1.0f + c) * 0.5f) / a0;
    b1_ = (-(1.0f + c)) / a0;
    b2_ = b0_;
  } else {
    b0_ = ((1.0f - c) * 0.5f) / a0;
    b1_ = (1.0f - c) / a0;
    b2_ = b0_;
  }
  a1_ = (-2.0f * c) / a0;
  a2_ = (1.0f - alpha) / a0;
  reset();
}

void Biquad::process(float* x, uint32_t frames) {
  if (x == nullptr || frames == 0) {
    return;
  }
  float z1 = z1_;
  float z2 = z2_;
  const float b0 = b0_;
  const float b1 = b1_;
  const float b2 = b2_;
  const float a1 = a1_;
  const float a2 = a2_;
  for (uint32_t i = 0; i < frames; ++i) {
    const float in = x[i];
    const float y = b0 * in + z1;
    z1 = b1 * in - a1 * y + z2;
    z2 = b2 * in - a2 * y;
    x[i] = y;
  }
  z1_ = z1;
  z2_ = z2;
}

void GistFilter::reset() {
  hp1_.reset();
  hp2_.reset();
  lp_.reset();
}

void GistFilter::set_rate(uint32_t sample_rate) {
  hp1_.set_highpass(sample_rate, kGistLowCutHz, kGistQ);
  hp2_.set_highpass(sample_rate, kGistLowCutHz, kGistQ);
  lp_.set_lowpass(sample_rate, kGistHighCutHz, kGistQ);
}

void GistFilter::process(float* x, uint32_t frames) {
  hp1_.process(x, frames);
  hp2_.process(x, frames);
  lp_.process(x, frames);
}

}  // namespace neon
