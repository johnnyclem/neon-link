#include "neon/ext_clock.hpp"

namespace neon {

namespace {
constexpr uint64_t kMilliBpmNumeratorQ8 = 60000000000ull * 256ull;
constexpr uint32_t kConsecutiveLongLimit = 3;
}  // namespace

void ExtClockEstimator::set_input_ppqn(uint32_t ppqn) {
  if (ppqn < 1) ppqn = 1;
  if (ppqn > 96) ppqn = 96;
  if (ppqn != ppqn_) {
    ppqn_ = ppqn;
    reset_history();
  }
}

void ExtClockEstimator::reset_history() {
  last_pulse_us_ = INT64_MIN;
  period_count_ = 0;
  period_next_ = 0;
  long_rejects_ = 0;
  ema_period_q8_ = 0;
  published_mbpm_ = 0;
  candidate_mbpm_ = 0;
  candidate_streak_ = 0;
  update_pending_ = false;
}

int64_t ExtClockEstimator::median_period() const {
  if (period_count_ == 0) {
    return 0;
  }
  int64_t sorted[kMedianWindow];
  const uint32_t n = period_count_;
  for (uint32_t i = 0; i < n; ++i) {
    const int64_t v = periods_[i];
    uint32_t j = i;
    while (j > 0 && sorted[j - 1] > v) {
      sorted[j] = sorted[j - 1];
      --j;
    }
    sorted[j] = v;
  }
  return sorted[n / 2];
}

void ExtClockEstimator::on_pulse(int64_t t_us) {
  if (last_pulse_us_ == INT64_MIN) {
    last_pulse_us_ = t_us;
    return;
  }
  const int64_t period = t_us - last_pulse_us_;
  if (period <= 0) {
    return;
  }

  const int64_t med = period_count_ >= 3 ? median_period() : 0;
  if (med > 0) {
    if (period < med / 2) {
      // Bounce/glitch: drop the edge, keep the previous anchor so a
      // genuinely faster clock self-heals on the next edge.
      return;
    }
    if (period > med * 2) {
      // Dropout: resync without recording. A genuinely slower clock will
      // keep landing here — after a few in a row, relock from scratch.
      last_pulse_us_ = t_us;
      static_assert(kConsecutiveLongLimit > 0, "");
      if (++long_rejects_ >= kConsecutiveLongLimit) {
        const int64_t keep = t_us;
        reset_history();
        last_pulse_us_ = keep;
        long_rejects_ = 0;
      }
      return;
    }
  }
  long_rejects_ = 0;
  last_pulse_us_ = t_us;

  periods_[period_next_] = period;
  period_next_ = (period_next_ + 1) % kMedianWindow;
  if (period_count_ < kMedianWindow) {
    ++period_count_;
  }
  if (period_count_ < 3) {
    return;
  }

  const int64_t med_now = median_period();
  const int64_t med_q8 = med_now << 8;
  if (ema_period_q8_ == 0) {
    ema_period_q8_ = med_q8;
  } else {
    ema_period_q8_ += (med_q8 - ema_period_q8_) / 8;
  }

  const uint64_t mpb_q8 =
      static_cast<uint64_t>(ema_period_q8_) * ppqn_;  // µs/beat, Q8
  if (mpb_q8 == 0) {
    return;
  }
  uint32_t mbpm = static_cast<uint32_t>(kMilliBpmNumeratorQ8 / mpb_q8);
  if (mbpm < 1000) mbpm = 1000;
  if (mbpm > 999000) mbpm = 999000;

  if (published_mbpm_ == 0) {
    published_mbpm_ = mbpm;
    pending_mbpm_ = mbpm;
    update_pending_ = true;
    return;
  }

  const int64_t diff = static_cast<int64_t>(mbpm) -
                       static_cast<int64_t>(published_mbpm_);
  const int64_t abs_diff = diff < 0 ? -diff : diff;
  if (abs_diff * kHysteresisDen <= published_mbpm_) {
    candidate_streak_ = 0;  // inside the band: nothing to do
    return;
  }

  // Outside the band: require the estimate to settle near a candidate
  // before publishing.
  const int64_t cdiff = static_cast<int64_t>(mbpm) -
                        static_cast<int64_t>(candidate_mbpm_);
  const int64_t abs_cdiff = cdiff < 0 ? -cdiff : cdiff;
  if (candidate_mbpm_ != 0 &&
      abs_cdiff * kHysteresisDen <= candidate_mbpm_) {
    ++candidate_streak_;
  } else {
    candidate_mbpm_ = mbpm;
    candidate_streak_ = 1;
  }
  if (candidate_streak_ >= kSettlePulses) {
    published_mbpm_ = mbpm;
    pending_mbpm_ = mbpm;
    update_pending_ = true;
    candidate_mbpm_ = 0;
    candidate_streak_ = 0;
  }
}

void ExtClockEstimator::on_reset(int64_t t_us) {
  phase_pending_ = true;
  phase_t_us_ = t_us;
}

bool ExtClockEstimator::active(int64_t now_us) {
  if (last_pulse_us_ == INT64_MIN) {
    return false;
  }
  int64_t timeout = kMinTimeoutUs;
  const int64_t expected = ema_period_q8_ >> 8;
  if (expected > 0 && expected * 4 > timeout) {
    timeout = expected * 4;
  }
  if (now_us - last_pulse_us_ > timeout) {
    reset_history();
    return false;
  }
  return true;
}

bool ExtClockEstimator::take_tempo_update(uint32_t* milli_bpm) {
  if (!update_pending_) {
    return false;
  }
  update_pending_ = false;
  *milli_bpm = pending_mbpm_;
  return true;
}

bool ExtClockEstimator::take_phase_request(int64_t* t_us) {
  if (!phase_pending_) {
    return false;
  }
  phase_pending_ = false;
  *t_us = phase_t_us_;
  return true;
}

}  // namespace neon
