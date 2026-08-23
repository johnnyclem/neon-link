#pragma once

// Per-peer clock-offset estimator (docs/NEON_SYNC.md §4). Two estimators,
// best one wins:
//
//  - Measured path: NTP-style four-timestamp ping/pong rounds. A sliding
//    window of (offset, rtt) samples; the estimate is the median offset of
//    the lowest-RTT quartile, which rejects the asymmetric-delay outliers
//    WiFi produces. Same shape as neon::SampleClock / ExtClockEstimator:
//    integer math, outlier gating, no doubles.
//
//  - TSF path: when both peers associate to the same BSS, the AP's beacon
//    TSF is a *shared* microsecond clock. offset = (my tsf−local) −
//    (peer tsf−local), median-of-window. Sub-millisecond by construction
//    and preferred over the measured path whenever it is available.
//
// Offset convention throughout: offset = peer_local_us − my_local_us at
// the same instant (a positive offset means the peer's clock reads ahead).

#include <cstdint>

namespace nsync {

class PeerClock {
 public:
  static constexpr uint32_t kWindow = 64;
  static constexpr uint32_t kTsfWindow = 5;
  // Samples older than this stop contributing (peer rebooted, path moved;
  // and under drift an old offset is simply wrong).
  static constexpr int64_t kSampleTtlUs = 30 * 1000000ll;
  // A round trip longer than this says nothing useful about offset.
  static constexpr int64_t kMaxUsableRttUs = 250000;

  // Measured path: one completed ping/pong round.
  void add_measured(int64_t offset_us, int64_t rtt_us, int64_t now_us);

  // TSF path: one offset derived from a shared-BSS TSF pair.
  void add_tsf(int64_t offset_us, int64_t now_us);

  // Low-confidence one-way seed (first ANNOUNCE heard from the peer,
  // biased by the one-way delay). Only used until a real sample lands.
  void seed(int64_t offset_us);

  bool valid(int64_t now_us) const;

  // Best current estimate. TSF wins while fresh; measured otherwise;
  // the seed only before either exists. 0 when !valid().
  int64_t offset_us(int64_t now_us) const;

  // True while a fresh TSF estimate is what offset_us() returns.
  bool using_tsf(int64_t now_us) const;

  // Round trip of the best measured sample (INT64_MAX before any).
  int64_t best_rtt_us() const { return best_rtt_us_; }

  void reset();

 private:
  struct Sample {
    int64_t offset_us;
    int64_t rtt_us;
    int64_t at_us;
  };

  int64_t measured_estimate(int64_t now_us, bool* ok) const;
  int64_t tsf_estimate(int64_t now_us, bool* ok) const;

  Sample samples_[kWindow] = {};
  uint32_t count_ = 0;
  uint32_t next_ = 0;
  int64_t best_rtt_us_ = INT64_MAX;

  int64_t tsf_samples_[kTsfWindow] = {};
  int64_t tsf_at_us_[kTsfWindow] = {};
  uint32_t tsf_count_ = 0;
  uint32_t tsf_next_ = 0;

  bool have_seed_ = false;
  int64_t seed_us_ = 0;
};

}  // namespace nsync
