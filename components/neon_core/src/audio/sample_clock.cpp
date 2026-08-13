#include "neon/audio/sample_clock.hpp"

#include "neon/fixed_math.hpp"

namespace neon {

namespace {

constexpr uint64_t kMicrosPerSecond = 1000000ull;

int64_t clamp_i64(int64_t v, int64_t lo, int64_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

void SampleClock::reset(uint32_t sample_rate) {
  sample_rate_ = sample_rate != 0 ? sample_rate : 44100;
  // (1e6 / rate) in Q32.32, computed without a division of the fraction:
  // 1e6 << 32 does not fit in 64 bits, so use the 128-bit helper.
  nominal_us_per_frame_q32_ =
      div_u128_u64(mul_u64(kMicrosPerSecond, 1ull << 32), sample_rate_);
  us_per_frame_q32_ = nominal_us_per_frame_q32_;
  have_anchor_ = false;
  have_base_ = false;
  origin_frame_ = 0;
  origin_us_ = 0;
  base_frame_ = 0;
  base_us_ = 0;
  last_mark_us_ = 0;
  residual_us_ = 0;
  ppm_ = 0;
  marks_ = 0;
}

void SampleClock::apply_ppm() {
  // rate = nominal * (1e6 + ppm) / 1e6. nominal is ~9.7e10 at 44.1 kHz, so
  // the product stays inside 64 bits for any ppm we allow.
  const uint64_t scale = static_cast<uint64_t>(1000000 + ppm_);
  us_per_frame_q32_ =
      div_u128_u64(mul_u64(nominal_us_per_frame_q32_, scale), kMicrosPerSecond);
}

void SampleClock::update(int64_t t_us, uint64_t frames) {
  if (nominal_us_per_frame_q32_ == 0) {
    reset(sample_rate_);
  }
  if (!have_anchor_) {
    have_anchor_ = true;
    origin_frame_ = frames;
    origin_us_ = t_us;
    last_mark_us_ = t_us;
    residual_us_ = 0;
    marks_ = 1;
    return;
  }

  const int64_t predicted = us_at_frame(frames);
  const int64_t err = t_us - predicted;
  residual_us_ = err;

  const int64_t dt_us = t_us - last_mark_us_;
  last_mark_us_ = t_us;

  if (err > kOutlierUs || err < -kOutlierUs || dt_us <= 0) {
    // A gap that large is a restart, an underrun, or a bogus mark — snap
    // the phase, drop the rate baseline, and leave the estimate alone.
    origin_frame_ = frames;
    origin_us_ = t_us;
    have_base_ = false;
    return;
  }

  // Phase loop: re-anchor on this mark, pulled a fraction of the way
  // toward the observed time. Re-anchoring every mark also keeps the
  // extrapolation distance (and therefore the multiply) small.
  origin_frame_ = frames;
  origin_us_ = predicted + (err >> kPhaseShift);
  if (marks_ < 0xffffffffu) {
    ++marks_;
  }

  // Rate loop: a straight two-point measurement between filtered anchors.
  if (!have_base_) {
    if (marks_ >= kBaseMarks) {
      have_base_ = true;
      base_frame_ = origin_frame_;
      base_us_ = origin_us_;
    }
    return;
  }
  const int64_t elapsed_frames = static_cast<int64_t>(frames - base_frame_);
  const int64_t elapsed_us = origin_us_ - base_us_;
  if (elapsed_frames <= 0 || elapsed_us < kMinBaselineUs) {
    return;
  }
  const uint64_t measured_q32 =
      div_u128_u64(mul_u64(static_cast<uint64_t>(elapsed_us), 1ull << 32),
                   static_cast<uint64_t>(elapsed_frames));
  // Same quantity in ppm against nominal, so the servo state stays small
  // and readable in the status document.
  const int64_t delta_q32 = static_cast<int64_t>(measured_q32) -
                            static_cast<int64_t>(nominal_us_per_frame_q32_);
  const int64_t ppm_measured =
      (delta_q32 * static_cast<int64_t>(kMicrosPerSecond)) /
      static_cast<int64_t>(nominal_us_per_frame_q32_);
  const int64_t step = clamp_i64((ppm_measured - ppm_) / kRateDivisor,
                                 -kMaxPpmStep, kMaxPpmStep);
  ppm_ = clamp_i32(ppm_ + static_cast<int32_t>(step), -kMaxPpm, kMaxPpm);
  apply_ppm();

  if (elapsed_us > kRebaseUs) {
    // Bounded memory, and slow crystal drift (temperature) still tracked.
    base_frame_ = origin_frame_;
    base_us_ = origin_us_;
  }
}

int64_t SampleClock::us_at_frame(uint64_t frame) const {
  if (!have_anchor_) {
    return 0;
  }
  const int64_t d = static_cast<int64_t>(frame - origin_frame_);
  const uint64_t rate = us_per_frame_q32_;
  // Split the Q32.32 multiply so the intermediate never overflows int64
  // even for an hour of frames: d * whole + (d * frac >> 32).
  const int64_t whole = static_cast<int64_t>(rate >> 32);
  const int64_t frac = static_cast<int64_t>(rate & 0xffffffffull);
  return origin_us_ + d * whole + ((d * frac) >> 32);
}

uint64_t SampleClock::frame_at_us(int64_t t_us) const {
  if (!have_anchor_ || us_per_frame_q32_ == 0) {
    return 0;
  }
  const int64_t d_us = t_us - origin_us_;
  const uint64_t mag = static_cast<uint64_t>(d_us < 0 ? -d_us : d_us);
  const uint64_t frames =
      div_u128_u64(mul_u64(mag, 1ull << 32), us_per_frame_q32_);
  if (d_us < 0) {
    return frames >= origin_frame_ ? 0 : origin_frame_ - frames;
  }
  return origin_frame_ + frames;
}

}  // namespace neon
