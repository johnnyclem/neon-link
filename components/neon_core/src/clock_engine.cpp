#include "neon/clock_engine.hpp"

#include "neon/fixed_math.hpp"

namespace neon {

uint64_t micros_per_beat_q32_from_milli_bpm(uint32_t milli_bpm) {
  if (milli_bpm < 1000) {
    milli_bpm = 1000;  // 1 BPM floor keeps (i << 32) inside uint64_t
  }
  const uint64_t n = 60000000000ull;  // µs per minute × 1000
  const uint64_t i = n / milli_bpm;
  const uint64_t r = n % milli_bpm;
  return (i << 32) | ((r << 32) / milli_bpm);
}

void ClockEngine::reset(int64_t origin_us) {
  rise_us_ = origin_us;
  rise_frac_ = 0;
  rem_acc_ = 0;
  fall_us_ = kNoFall;
  running_ = true;
}

void ClockEngine::set_tempo(uint64_t micros_per_beat_q32) {
  mpb_q32_ = micros_per_beat_q32;
  recompute_period();
}

void ClockEngine::set_output(const OutputSettings& s) {
  out_ = s;
  if (out_.ppqn == 0) {
    out_.ppqn = 1;
  }
  recompute_period();
}

void ClockEngine::recompute_period() {
  if (out_.ppqn == 0 || mpb_q32_ == 0) {
    period_q32_ = 0;
    period_rem_ = 0;
    return;
  }
  period_q32_ = mpb_q32_ / out_.ppqn;
  period_rem_ = static_cast<uint32_t>(mpb_q32_ % out_.ppqn);
  if (rem_acc_ >= out_.ppqn) {
    rem_acc_ = 0;
  }
}

void ClockEngine::advance_rise() {
  uint64_t add = period_q32_;
  rem_acc_ += period_rem_;
  if (rem_acc_ >= out_.ppqn) {
    add += 1;
    rem_acc_ -= out_.ppqn;
  }
  const uint64_t frac_sum =
      static_cast<uint64_t>(rise_frac_) + (add & 0xffffffffull);
  rise_frac_ = static_cast<uint32_t>(frac_sum);
  rise_us_ += static_cast<int64_t>(add >> 32) +
              static_cast<int64_t>(frac_sum >> 32);
}

void ClockEngine::set_transport_gating(bool enabled) {
  gate_transport_ = enabled;
}

void ClockEngine::retime(const TimelineSnapshot& tl, int64_t from_us) {
  mpb_q32_ = tl.tempo_mpb_q32;
  recompute_period();
  running_ = gate_transport_ ? tl.playing != 0 : true;
  if (!running_ || period_q32_ == 0) {
    return;  // pending fall (if any) still drains through generate()
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

  // First tick at or after that beat: k = ceil(beat * ppqn / 2^32).
  const uint64_t abs_beat = beat_q32 < 0 ? static_cast<uint64_t>(-beat_q32)
                                         : static_cast<uint64_t>(beat_q32);
  const uint64_t scaled = abs_beat * out_.ppqn;  // |beat| < 2^55, ppqn small
  int64_t k;
  if (beat_q32 >= 0) {
    k = static_cast<int64_t>(scaled >> 32) + ((scaled & 0xffffffffull) ? 1 : 0);
  } else {
    k = -static_cast<int64_t>(scaled >> 32);  // ceil of a negative value
  }

  anchor_tick(tl, k);
  // Rounding guard: the anchor must not sit before the window start.
  while (rise_us_ < from_us) {
    advance_rise();
  }
}

// Places rise_us_/rise_frac_ exactly at tick k of the session grid
// (beat k/ppqn) and initializes the remainder accumulator so subsequent
// advance_rise() calls continue the same absolute grid.
void ClockEngine::anchor_tick(const TimelineSnapshot& tl, int64_t k) {
  // Beat of tick k in Q32.32, floor((k << 32) / ppqn); the dropped
  // remainder is < 2^-32 beats.
  const int64_t tick_beat_q32 =
      k >= 0 ? static_cast<int64_t>((static_cast<uint64_t>(k) << 32) /
                                    out_.ppqn)
             : -static_cast<int64_t>(
                   ((static_cast<uint64_t>(-k) << 32) + out_.ppqn - 1) /
                   out_.ppqn);

  // t(k) = origin + (tick_beat - beat_at_origin) * mpb, done as an exact
  // Q32.32 × Q32.32 product of the (small) beat delta.
  const int64_t d_q32 = tick_beat_q32 - tl.beat_at_origin_q32;
  const uint64_t abs_d = d_q32 < 0 ? static_cast<uint64_t>(-d_q32)
                                   : static_cast<uint64_t>(d_q32);
  const U128 p = mul_u64(abs_d, mpb_q32_);  // Q64.64
  const uint64_t off_us = p.hi;             // integer µs
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

  // rem_acc after computing tick k's time is (k * (mpb % ppqn)) mod ppqn.
  const uint32_t ppqn = out_.ppqn;
  const uint64_t rem = period_rem_;  // = mpb_q32 % ppqn, < ppqn
  const uint64_t k_mod =
      k >= 0 ? static_cast<uint64_t>(k) % ppqn
             : (ppqn - (static_cast<uint64_t>(-k) % ppqn)) % ppqn;
  rem_acc_ = static_cast<uint32_t>((k_mod * rem) % ppqn);
}

int64_t ClockEngine::effective_trig_len() const {
  int64_t period_int = static_cast<int64_t>(period_q32_ >> 32);
  if (period_int < 2) {
    period_int = 2;
  }
  const int64_t max_len = period_int - 1;
  const int64_t want = static_cast<int64_t>(out_.trig_len_us);
  return want < max_len ? (want < 1 ? 1 : want) : max_len;
}

size_t ClockEngine::generate(int64_t t0_us, int64_t t1_us, Edge* out,
                             size_t max_out) {
  size_t n = 0;
  while (n < max_out) {
    const bool fall_pending = fall_us_ != kNoFall;
    // When stopped (transport gating) only the in-flight pulse drains; no
    // new rising edges are scheduled.
    const bool can_rise = running_ && period_q32_ != 0;
    if (!can_rise && !fall_pending) {
      break;
    }
    const bool take_fall = fall_pending && (!can_rise || fall_us_ <= rise_us_);
    const int64_t t = take_fall ? fall_us_ : rise_us_;
    if (t >= t1_us) {
      break;
    }
    if (take_fall) {
      if (t >= t0_us) {
        out[n++] = Edge{t, 0, false};
      }
      fall_us_ = kNoFall;
    } else {
      if (t >= t0_us) {
        out[n++] = Edge{t, 0, true};
      }
      fall_us_ = t + effective_trig_len();
      advance_rise();
    }
  }
  return n;
}

}  // namespace neon
