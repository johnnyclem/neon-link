#include "neon/audio/resampler.hpp"

#include <cstddef>

#include "neon/fixed_math.hpp"

namespace neon {

namespace {
constexpr float kScale = 1.0f / 32768.0f;
}

void LinearResampler::reset() { pos_q32_ = 0; }

void LinearResampler::set_rates(uint32_t in_rate, uint32_t out_rate) {
  in_rate_ = in_rate != 0 ? in_rate : 44100;
  out_rate_ = out_rate != 0 ? out_rate : 44100;
  recompute();
}

void LinearResampler::set_trim_ppm(int32_t ppm) {
  if (ppm > kMaxTrimPpm) ppm = kMaxTrimPpm;
  if (ppm < -kMaxTrimPpm) ppm = -kMaxTrimPpm;
  trim_ppm_ = ppm;
  recompute();
}

void LinearResampler::recompute() {
  // step = in/out, Q32.32, scaled by (1e6 + trim)/1e6. Equal rates with no
  // trim must land on exactly 1.0 so the ratio-1 path stays bit-exact.
  const uint64_t base = q32_div(in_rate_, out_rate_);
  if (trim_ppm_ == 0) {
    step_q32_ = base;
    return;
  }
  const uint64_t scale = static_cast<uint64_t>(1000000 + trim_ppm_);
  step_q32_ = div_u128_u64(mul_u64(base, scale), 1000000ull);
}

uint32_t LinearResampler::process(const int16_t* in, uint32_t in_frames,
                                  uint32_t* consumed, float* l, float* r,
                                  uint32_t out_frames) {
  uint32_t produced = 0;
  if (in == nullptr || in_frames == 0 || out_frames == 0) {
    if (consumed != nullptr) {
      *consumed = 0;
    }
    return 0;
  }
  while (produced < out_frames) {
    const uint64_t i = pos_q32_ >> 32;
    const uint32_t frac = static_cast<uint32_t>(pos_q32_ & 0xffffffffull);
    if (i >= in_frames) {
      break;
    }
    if (frac != 0 && i + 1 >= in_frames) {
      break;  // the interpolation partner has not arrived yet
    }
    const size_t idx = static_cast<size_t>(i) * 2;
    float lv = static_cast<float>(in[idx]) * kScale;
    float rv = static_cast<float>(in[idx + 1]) * kScale;
    if (frac != 0) {
      const float f = static_cast<float>(frac) * (1.0f / 4294967296.0f);
      const float l1 = static_cast<float>(in[idx + 2]) * kScale;
      const float r1 = static_cast<float>(in[idx + 3]) * kScale;
      lv += (l1 - lv) * f;
      rv += (r1 - rv) * f;
    }
    if (l != nullptr) l[produced] = lv;
    if (r != nullptr) r[produced] = rv;
    ++produced;
    pos_q32_ += step_q32_;
  }
  const uint64_t whole = pos_q32_ >> 32;
  const uint32_t take =
      whole > in_frames ? in_frames : static_cast<uint32_t>(whole);
  pos_q32_ -= static_cast<uint64_t>(take) << 32;
  if (consumed != nullptr) {
    *consumed = take;
  }
  return produced;
}

}  // namespace neon
