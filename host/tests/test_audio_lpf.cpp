#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "neon/audio/lpf.hpp"

namespace {

std::vector<float> sine(uint32_t rate, double freq, uint32_t frames) {
  std::vector<float> out(frames);
  for (uint32_t i = 0; i < frames; ++i) {
    out[i] = static_cast<float>(std::sin(2.0 * M_PI * freq * i / rate));
  }
  return out;
}

float rms(const std::vector<float>& x, uint32_t skip) {
  double acc = 0.0;
  uint32_t n = 0;
  for (uint32_t i = skip; i < x.size(); ++i) {
    acc += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    ++n;
  }
  return n == 0 ? 0.0f : static_cast<float>(std::sqrt(acc / n));
}

}  // namespace

TEST_CASE("gist LPF: DC passes, silence stays silence") {
  neon::BiquadLowpass lp;
  lp.set_lowpass(48000, neon::kGistCutoffHz, neon::kGistQ);

  std::vector<float> dc(512, 0.5f);
  lp.process(dc.data(), static_cast<uint32_t>(dc.size()));
  // Settled DC gain of an RBJ lowpass is 1.
  CHECK(dc.back() == doctest::Approx(0.5f).epsilon(0.02));

  lp.reset();
  std::vector<float> z(256, 0.0f);
  lp.process(z.data(), static_cast<uint32_t>(z.size()));
  for (float v : z) {
    CHECK(v == 0.0f);
  }
}

TEST_CASE("gist LPF: 200 Hz passes, 12 kHz is dumped") {
  neon::BiquadLowpass lp;
  lp.set_lowpass(48000, neon::kGistCutoffHz, neon::kGistQ);

  auto lo = sine(48000, 200.0, 4096);
  const float in_lo = rms(lo, 2048);
  lp.process(lo.data(), static_cast<uint32_t>(lo.size()));
  const float out_lo = rms(lo, 2048);
  CHECK(out_lo / in_lo == doctest::Approx(1.0f).epsilon(0.05));

  lp.reset();
  auto hi = sine(48000, 12000.0, 4096);
  const float in_hi = rms(hi, 2048);
  lp.process(hi.data(), static_cast<uint32_t>(hi.size()));
  const float out_hi = rms(hi, 2048);
  // 2-pole Butterworth an octave-plus above fc: at least -12 dB.
  CHECK(out_hi / in_hi < 0.25f);
}

TEST_CASE("gist LPF: null / empty is a no-op") {
  neon::BiquadLowpass lp;
  lp.set_lowpass(48000, neon::kGistCutoffHz, neon::kGistQ);
  lp.process(nullptr, 64);
  float x = 1.0f;
  lp.process(&x, 0);
  CHECK(x == 1.0f);
}

TEST_CASE("gist HPF: 40 Hz is dumped, 400 Hz passes") {
  neon::Biquad hp;
  hp.set_highpass(48000, neon::kGistLowCutHz, neon::kGistQ);

  auto rumble = sine(48000, 40.0, 8192);
  const float in_lo = rms(rumble, 4096);
  hp.process(rumble.data(), static_cast<uint32_t>(rumble.size()));
  const float out_lo = rms(rumble, 4096);
  // One 2-pole section at 120 Hz: 40 Hz is ~1.5 octaves down, ~-18 dB.
  CHECK(out_lo / in_lo < 0.20f);

  hp.reset();
  auto mid = sine(48000, 400.0, 4096);
  const float in_mid = rms(mid, 2048);
  hp.process(mid.data(), static_cast<uint32_t>(mid.size()));
  const float out_mid = rms(mid, 2048);
  CHECK(out_mid / in_mid == doctest::Approx(1.0f).epsilon(0.08));
}

TEST_CASE("gist band: 40 Hz gone, 400 Hz kept, 12 kHz gone") {
  neon::GistFilter gist;
  gist.set_rate(48000);

  auto rumble = sine(48000, 40.0, 8192);
  const float in_lo = rms(rumble, 4096);
  gist.process(rumble.data(), static_cast<uint32_t>(rumble.size()));
  // Two 120 Hz sections: 40 Hz should be at least -30 dB.
  CHECK(rms(rumble, 4096) / in_lo < 0.04f);

  gist.reset();
  auto mid = sine(48000, 400.0, 4096);
  const float in_mid = rms(mid, 2048);
  gist.process(mid.data(), static_cast<uint32_t>(mid.size()));
  CHECK(rms(mid, 2048) / in_mid == doctest::Approx(1.0f).epsilon(0.10));

  gist.reset();
  auto hi = sine(48000, 12000.0, 4096);
  const float in_hi = rms(hi, 2048);
  gist.process(hi.data(), static_cast<uint32_t>(hi.size()));
  CHECK(rms(hi, 2048) / in_hi < 0.25f);

  gist.reset();
  std::vector<float> dc(1024, 0.5f);
  gist.process(dc.data(), static_cast<uint32_t>(dc.size()));
  CHECK(std::fabs(dc.back()) < 0.02f);
}
