#include "neon/pulse_channel.hpp"

#include "neon/fixed_math.hpp"

namespace neon {

namespace {
uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
}  // namespace

void PulseChannel::configure(const ClockOutputConfig& cfg) {
  cfg_ = cfg;
  cfg_.ppqn = clamp_u32(cfg_.ppqn, 1, 192);
  cfg_.mult = clamp_u32(cfg_.mult, 1, 16);
  cfg_.div = clamp_u32(cfg_.div, 1, 16);
  if (cfg_.duty_pct < 1) cfg_.duty_pct = 1;
  if (cfg_.duty_pct > 99) cfg_.duty_pct = 99;
  if (cfg_.shuffle_pct > 75) cfg_.shuffle_pct = 75;
  rate_p_ = cfg_.ppqn * cfg_.mult;
  rate_q_ = cfg_.div;
  recompute_period();
}

void PulseChannel::set_latency(int32_t latency_us) { latency_us_ = latency_us; }

void PulseChannel::recompute_period() {
  if (mpb_q32_ == 0 || rate_p_ == 0) {
    period_q32_ = 0;
    period_rem_ = 0;
    return;
  }
  // period = mpb * q / p, exact as quotient + remainder over p.
  const U128 num = mul_u64(mpb_q32_, rate_q_);
  uint64_t rem = 0;
  period_q32_ = div_u128_u64_rem(num, rate_p_, &rem);
  period_rem_ = static_cast<uint32_t>(rem);
  if (rem_acc_ >= rate_p_) {
    rem_acc_ = 0;
  }
}

void PulseChannel::advance_tick() {
  uint64_t add = period_q32_;
  rem_acc_ += period_rem_;
  if (rem_acc_ >= rate_p_) {
    add += 1;
    rem_acc_ -= rate_p_;
  }
  const uint64_t frac_sum =
      static_cast<uint64_t>(rise_frac_) + (add & 0xffffffffull);
  rise_frac_ = static_cast<uint32_t>(frac_sum);
  rise_us_ += static_cast<int64_t>(add >> 32) +
              static_cast<int64_t>(frac_sum >> 32);
  ++tick_index_;
}

int64_t PulseChannel::swing_us() const {
  if (cfg_.shuffle_pct == 0 || (tick_index_ & 1) == 0) {
    return 0;
  }
  const int64_t period_int = static_cast<int64_t>(period_q32_ >> 32);
  return period_int * cfg_.shuffle_pct / 100;
}

int64_t PulseChannel::shuffled_rise() const { return rise_us_ + swing_us(); }

void PulseChannel::retime(const TimelineSnapshot& tl, int64_t from_us,
                          bool running) {
  mpb_q32_ = tl.tempo_mpb_q32;
  recompute_period();
  running_ = running && cfg_.enabled;
  if (!running_ || period_q32_ == 0) {
    return;  // pending fall (if any) still drains
  }

  // Session beat at from_us, Q32.32 signed.
  const int64_t dt_us = from_us - tl.origin_us;
  const uint64_t abs_dt = dt_us < 0 ? static_cast<uint64_t>(-dt_us)
                                    : static_cast<uint64_t>(dt_us);
  const uint64_t dbeat_q32 =
      div_u128_u64(U128{abs_dt >> 32, abs_dt << 32}, mpb_q32_);
  const int64_t beat_q32 =
      tl.beat_at_origin_q32 +
      (dt_us < 0 ? -static_cast<int64_t>(dbeat_q32)
                 : static_cast<int64_t>(dbeat_q32));

  // First tick at or after that beat: k = ceil(beat * p / q / 2^32).
  const uint64_t abs_beat = beat_q32 < 0 ? static_cast<uint64_t>(-beat_q32)
                                         : static_cast<uint64_t>(beat_q32);
  const U128 scaled_n = mul_u64(abs_beat, rate_p_);
  uint64_t scaled_rem = 0;
  const uint64_t scaled = div_u128_u64_rem(scaled_n, rate_q_, &scaled_rem);
  int64_t k;
  if (beat_q32 >= 0) {
    k = static_cast<int64_t>(scaled >> 32) +
        (((scaled & 0xffffffffull) != 0 || scaled_rem != 0) ? 1 : 0);
  } else {
    k = -static_cast<int64_t>(scaled >> 32);  // ceil of a negative value
  }

  // Beat of tick k in Q32.32: floor(k * q << 32 / p) (< 2^-32 beat error).
  const uint64_t abs_k = k < 0 ? static_cast<uint64_t>(-k)
                               : static_cast<uint64_t>(k);
  const U128 kq = mul_u64(abs_k * rate_q_, 1ull << 32);
  int64_t tick_beat_q32;
  if (k >= 0) {
    tick_beat_q32 = static_cast<int64_t>(div_u128_u64(kq, rate_p_));
  } else {
    // ceil for the magnitude of a negative value
    uint64_t rem = 0;
    const uint64_t qmag = div_u128_u64_rem(kq, rate_p_, &rem);
    tick_beat_q32 = -static_cast<int64_t>(qmag + (rem != 0 ? 1 : 0));
  }

  // t(k) = origin + (tick_beat - beat_at_origin) * mpb, exact Q64.64.
  const int64_t d_q32 = tick_beat_q32 - tl.beat_at_origin_q32;
  const uint64_t abs_d = d_q32 < 0 ? static_cast<uint64_t>(-d_q32)
                                   : static_cast<uint64_t>(d_q32);
  const U128 p = mul_u64(abs_d, mpb_q32_);
  const uint64_t off_us = p.hi;
  const uint32_t off_frac = static_cast<uint32_t>(p.lo >> 32);

  if (d_q32 >= 0) {
    rise_us_ = tl.origin_us + static_cast<int64_t>(off_us);
    rise_frac_ = off_frac;
  } else if (off_frac == 0) {
    rise_us_ = tl.origin_us - static_cast<int64_t>(off_us);
    rise_frac_ = 0;
  } else {
    rise_us_ = tl.origin_us - static_cast<int64_t>(off_us) - 1;
    rise_frac_ = static_cast<uint32_t>((1ull << 32) - off_frac);
  }
  tick_index_ = k;

  // rem_acc after computing tick k's time is (k * (mpb*q % p)) mod p.
  const uint64_t k_mod =
      k >= 0 ? abs_k % rate_p_ : (rate_p_ - (abs_k % rate_p_)) % rate_p_;
  rem_acc_ = static_cast<uint32_t>((k_mod * period_rem_) % rate_p_);

  // Rounding guard: the anchor must not sit before the window start.
  while (rise_us_ < from_us) {
    advance_tick();
  }
}

int64_t PulseChannel::next_shuffled_rise_after_advance() {
  advance_tick();
  return shuffled_rise();
}

bool PulseChannel::peek(Edge* out) const {
  const bool fall_pending = fall_us_ != kNoFall;
  const bool can_rise = running_ && period_q32_ != 0;
  if (!fall_pending && !can_rise) {
    return false;
  }
  if (fall_pending && (!can_rise || fall_us_ <= shuffled_rise())) {
    out->t_us = fall_us_ + latency_us_;
    out->high = false;
  } else {
    out->t_us = shuffled_rise() + latency_us_;
    out->high = true;
  }
  out->channel = 0;
  return true;
}

void PulseChannel::pop() {
  const bool fall_pending = fall_us_ != kNoFall;
  const bool can_rise = running_ && period_q32_ != 0;
  if (fall_pending && (!can_rise || fall_us_ <= shuffled_rise())) {
    fall_us_ = kNoFall;
    return;
  }
  if (!can_rise) {
    return;
  }
  // Consume the rise: compute this pulse's fall, bounded by the next rise
  // so every cycle keeps a low phase even with high duty + shuffle.
  const int64_t rise = shuffled_rise();
  const int64_t period_int = static_cast<int64_t>(period_q32_ >> 32);
  int64_t len;
  if (cfg_.mode == ClockOutputConfig::PulseMode::kSquare) {
    len = period_int * cfg_.duty_pct / 100;
  } else {
    len = static_cast<int64_t>(cfg_.trig_len_us);
  }
  if (len < 1) {
    len = 1;
  }
  const int64_t next_rise = next_shuffled_rise_after_advance();
  int64_t fall = rise + len;
  if (fall >= next_rise) {
    fall = next_rise - 1;
  }
  if (fall <= rise) {
    fall = rise + 1;
  }
  fall_us_ = fall;
}

}  // namespace neon
