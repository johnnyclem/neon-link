#include "nsync/peer_clock.hpp"

#include <algorithm>

namespace nsync {

void PeerClock::add_measured(int64_t offset_us, int64_t rtt_us,
                             int64_t now_us) {
  if (rtt_us < 0 || rtt_us > kMaxUsableRttUs) {
    return;
  }
  samples_[next_] = Sample{offset_us, rtt_us, now_us};
  next_ = (next_ + 1) % kWindow;
  if (count_ < kWindow) {
    ++count_;
  }
  if (rtt_us < best_rtt_us_) {
    best_rtt_us_ = rtt_us;
  }
}

void PeerClock::add_tsf(int64_t offset_us, int64_t now_us) {
  tsf_samples_[tsf_next_] = offset_us;
  tsf_at_us_[tsf_next_] = now_us;
  tsf_next_ = (tsf_next_ + 1) % kTsfWindow;
  if (tsf_count_ < kTsfWindow) {
    ++tsf_count_;
  }
}

void PeerClock::seed(int64_t offset_us) {
  if (!have_seed_ && count_ == 0 && tsf_count_ == 0) {
    have_seed_ = true;
    seed_us_ = offset_us;
  }
}

bool PeerClock::valid(int64_t now_us) const {
  bool ok = false;
  tsf_estimate(now_us, &ok);
  if (ok) {
    return true;
  }
  measured_estimate(now_us, &ok);
  return ok || have_seed_;
}

int64_t PeerClock::offset_us(int64_t now_us) const {
  bool ok = false;
  const int64_t tsf = tsf_estimate(now_us, &ok);
  if (ok) {
    return tsf;
  }
  const int64_t measured = measured_estimate(now_us, &ok);
  if (ok) {
    return measured;
  }
  return have_seed_ ? seed_us_ : 0;
}

bool PeerClock::using_tsf(int64_t now_us) const {
  bool ok = false;
  tsf_estimate(now_us, &ok);
  return ok;
}

void PeerClock::reset() {
  count_ = 0;
  next_ = 0;
  best_rtt_us_ = INT64_MAX;
  tsf_count_ = 0;
  tsf_next_ = 0;
  have_seed_ = false;
  seed_us_ = 0;
}

namespace {

int64_t median_of(int64_t* v, uint32_t n) {
  std::sort(v, v + n);
  return n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

}  // namespace

int64_t PeerClock::measured_estimate(int64_t now_us, bool* ok) const {
  Sample fresh[kWindow];
  uint32_t n = 0;
  for (uint32_t i = 0; i < count_; ++i) {
    if (now_us - samples_[i].at_us <= kSampleTtlUs) {
      fresh[n++] = samples_[i];
    }
  }
  if (n == 0) {
    *ok = false;
    return 0;
  }

  // First-order drift term: crystals differ by tens of ppm, which walks
  // the true offset several hundred µs across the sample window — a plain
  // windowed median would lag by half of that. Estimate the rate from the
  // older-half vs newer-half medians (jitter cancels in the medians) and
  // project every sample to `now` before filtering.
  int64_t rate_num = 0;
  int64_t rate_den = 1;
  if (n >= 8) {
    Sample by_time[kWindow];
    for (uint32_t i = 0; i < n; ++i) {
      by_time[i] = fresh[i];
    }
    std::sort(by_time, by_time + n, [](const Sample& a, const Sample& b) {
      return a.at_us < b.at_us;
    });
    const uint32_t half = n / 2;
    int64_t old_off[kWindow];
    int64_t new_off[kWindow];
    for (uint32_t i = 0; i < half; ++i) {
      old_off[i] = by_time[i].offset_us;
    }
    const uint32_t new_n = n - half;
    for (uint32_t i = 0; i < new_n; ++i) {
      new_off[i] = by_time[half + i].offset_us;
    }
    const int64_t t_old = by_time[half / 2].at_us;
    const int64_t t_new = by_time[half + new_n / 2].at_us;
    const int64_t dt = t_new - t_old;
    // A short baseline turns median jitter into a large phantom slope
    // (which projection then amplifies) — demand a real one, and cap the
    // result at ±300 ppm: no sane pair of crystals differs by more, so
    // anything larger is a step (reboot, route change), not drift.
    if (dt >= 6000000) {
      int64_t doff = median_of(new_off, new_n) - median_of(old_off, half);
      const int64_t cap = 3 * dt / 10000;
      if (doff > cap) {
        doff = cap;
      }
      if (doff < -cap) {
        doff = -cap;
      }
      rate_num = doff;
      rate_den = dt;
    }
  }

  // Lowest-effective-RTT quartile (at least one sample), then the median
  // projected offset of that quartile. Minimum-RTT samples have the least
  // room for asymmetric queueing delay, so their offsets are the
  // trustworthy ones — but a stale lucky sample is worth less than a
  // recent decent one under drift, so age charges 200 µs of penalty per
  // second when ranking.
  std::sort(fresh, fresh + n, [now_us](const Sample& a, const Sample& b) {
    return a.rtt_us + (now_us - a.at_us) / 5000 <
           b.rtt_us + (now_us - b.at_us) / 5000;
  });
  uint32_t q = n / 3;
  if (q == 0) {
    q = 1;
  }
  int64_t offs[kWindow];
  for (uint32_t i = 0; i < q; ++i) {
    offs[i] =
        fresh[i].offset_us + rate_num * (now_us - fresh[i].at_us) / rate_den;
  }
  *ok = true;
  return median_of(offs, q);
}

int64_t PeerClock::tsf_estimate(int64_t now_us, bool* ok) const {
  // TSF pairs go stale fast if a peer roams to another BSS; require a
  // recent sample (the sender hints every ~2 s).
  constexpr int64_t kTsfTtlUs = 10 * 1000000ll;
  int64_t fresh[kTsfWindow];
  int64_t fresh_at[kTsfWindow];
  uint32_t n = 0;
  for (uint32_t i = 0; i < tsf_count_; ++i) {
    if (now_us - tsf_at_us_[i] <= kTsfTtlUs) {
      fresh[n] = tsf_samples_[i];
      fresh_at[n] = tsf_at_us_[i];
      ++n;
    }
  }
  if (n < 2) {
    *ok = false;
    return 0;
  }
  // Same drift story as the measured path, just with cleaner samples:
  // project each one to `now` with the slope of the window's endpoints
  // (samples arrive in ring order, which is time order).
  uint32_t oldest = 0;
  uint32_t newest = 0;
  for (uint32_t i = 1; i < n; ++i) {
    if (fresh_at[i] < fresh_at[oldest]) {
      oldest = i;
    }
    if (fresh_at[i] > fresh_at[newest]) {
      newest = i;
    }
  }
  const int64_t dt = fresh_at[newest] - fresh_at[oldest];
  int64_t rate_num = 0;
  int64_t rate_den = 1;
  if (dt >= 3000000) {
    int64_t doff = fresh[newest] - fresh[oldest];
    const int64_t cap = dt / 1000;  // ±1000 ppm sanity bound
    if (doff > cap) {
      doff = cap;
    }
    if (doff < -cap) {
      doff = -cap;
    }
    rate_num = doff;
    rate_den = dt;
  }
  int64_t proj[kTsfWindow];
  for (uint32_t i = 0; i < n; ++i) {
    proj[i] = fresh[i] + rate_num * (now_us - fresh_at[i]) / rate_den;
  }
  std::sort(proj, proj + n);
  *ok = true;
  return n % 2 == 1 ? proj[n / 2] : (proj[n / 2 - 1] + proj[n / 2]) / 2;
}

}  // namespace nsync
