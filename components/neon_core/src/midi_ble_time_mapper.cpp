#include "neon/midi/ble_time_mapper.hpp"

#include <algorithm>

namespace neon {
namespace midi {

namespace {

// floor(a / b) for b > 0, correct for negative a.
int64_t floor_div(int64_t a, int64_t b) {
  int64_t q = a / b;
  if (a % b != 0 && a < 0) {
    --q;
  }
  return q;
}

int64_t median_of(int64_t* v, uint32_t n) {
  std::sort(v, v + n);
  return n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

}  // namespace

void BleTimeMapper::reset() {
  have_last_ = false;
  last_arrival_us_ = 0;
  last_ms_ = 0;
  sender_ms_unwrapped_ = 0;
  offset_count_ = 0;
  offset_next_ = 0;
  delta_count_ = 0;
  delta_next_ = 0;
  clean_streak_ = 0;
  trusted_ = false;
}

// Comb test on the decoded spacing: median delta and the mean absolute
// deviation around it. Real stamps deviate by about the 1 ms stamp
// granularity; a degenerate sender's deltas are the arrival comb (zeros
// inside a packet, connection-interval jumps between), whose deviation
// is the same order as the median itself.
bool BleTimeMapper::spacing_clean() const {
  int64_t sorted[kWindow];
  for (uint32_t i = 0; i < delta_count_; ++i) {
    sorted[i] = deltas_ms_[i];
  }
  const int64_t m = median_of(sorted, delta_count_);
  if (m <= 0) {
    return false;  // all-zero (or backwards) spacing: pure bucket stamps
  }
  int64_t dev_sum = 0;
  for (uint32_t i = 0; i < delta_count_; ++i) {
    const int64_t d = deltas_ms_[i] - m;
    dev_sum += d < 0 ? -d : d;
  }
  return dev_sum * kDispersionNum <= m * static_cast<int64_t>(delta_count_);
}

int64_t BleTimeMapper::on_tick(int64_t arrival_us, uint16_t sender_ms13) {
  const int32_t ms = static_cast<int32_t>(sender_ms13 & 0x1fffu);

  if (!have_last_ || arrival_us - last_arrival_us_ > kResetGapUs) {
    reset();
    have_last_ = true;
    last_arrival_us_ = arrival_us;
    last_ms_ = ms;
    sender_ms_unwrapped_ = ms;
    offsets_[0] = arrival_us - sender_ms_unwrapped_ * 1000;
    offset_at_us_[0] = arrival_us;
    offset_count_ = 1;
    offset_next_ = 1 % kWindow;
    return arrival_us;
  }

  // Unwrap: the 13-bit delta is ambiguous by multiples of 8192 ms; the
  // arrival gap — coarse, but bounded by kResetGapUs, well under half the
  // modulus — picks the wrap count. k lands at −1 for a stamp that steps
  // slightly backwards (which the raw 0..8191 delta would otherwise read
  // as a near-full wrap forward), 0 for the common case.
  const int64_t d_raw = static_cast<int64_t>(
      static_cast<uint32_t>(ms - last_ms_) &
      static_cast<uint32_t>(kModulusMs - 1));
  const int64_t expected_ms = (arrival_us - last_arrival_us_) / 1000;
  const int64_t k =
      floor_div(expected_ms - d_raw + kModulusMs / 2, kModulusMs);
  const int64_t d_ms = d_raw + k * kModulusMs;
  sender_ms_unwrapped_ += d_ms;
  last_arrival_us_ = arrival_us;
  last_ms_ = ms;

  deltas_ms_[delta_next_] = static_cast<int32_t>(d_ms);
  delta_next_ = (delta_next_ + 1) % kWindow;
  if (delta_count_ < kWindow) {
    ++delta_count_;
  }

  offsets_[offset_next_] = arrival_us - sender_ms_unwrapped_ * 1000;
  offset_at_us_[offset_next_] = arrival_us;
  offset_next_ = (offset_next_ + 1) % kWindow;
  if (offset_count_ < kWindow) {
    ++offset_count_;
  }

  // Trust is earned over a beat of clean spacing and revoked by a single
  // comb-like window: decoded spacing is immune to link jitter, so a
  // dirty window indicts the sender's stamping, not the radio.
  if (delta_count_ == kWindow) {
    clean_streak_ = spacing_clean() ? clean_streak_ + 1 : 0;
  }
  trusted_ = clean_streak_ >= kTrustEvals;
  if (!trusted_) {
    return arrival_us;
  }

  // Median of the drift-fresh offsets: the sender grid at typical
  // delivery latency. The constant part is invisible to the PLL; the
  // jitter that poisons raw arrivals is what the median removes.
  int64_t fresh[kWindow];
  uint32_t n = 0;
  for (uint32_t i = 0; i < offset_count_; ++i) {
    if (arrival_us - offset_at_us_[i] <= kSampleTtlUs) {
      fresh[n++] = offsets_[i];
    }
  }
  if (n == 0) {
    return arrival_us;  // unreachable: this tick's sample is fresh
  }
  return sender_ms_unwrapped_ * 1000 + median_of(fresh, n);
}

}  // namespace midi
}  // namespace neon
