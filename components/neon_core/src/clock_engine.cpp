#include "neon/clock_engine.hpp"

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
  if (!running_ || period_q32_ == 0) {
    return 0;
  }
  size_t n = 0;
  while (n < max_out) {
    const bool fall_pending = fall_us_ != kNoFall;
    const bool take_fall = fall_pending && fall_us_ <= rise_us_;
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
