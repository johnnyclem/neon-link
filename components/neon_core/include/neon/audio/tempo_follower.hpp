#pragma once

// Audio-in tempo follow: session-tempo IOI classifier over a contained
// ExtClockEstimator. take_tempo_update is the one-shot tempo proposal.

#include <cstdint>

#include "neon/ext_clock.hpp"

namespace neon {

class AudioTempoFollower {
 public:
  enum class Lock : uint8_t { kIdle = 0, kAcquiring = 1, kLocked = 2 };

  static constexpr uint32_t kMinIoIs = 4;
  static constexpr uint32_t kVoteRun = 3;
  static constexpr int64_t kTempoGapUs = 1000000;
  static constexpr int64_t kMinTimeoutUs = 2000000;
  static constexpr uint32_t kIntegerGuardMbp = 600;
  static constexpr uint32_t kSlewMbp = 2000;

  void set_session_tempo(uint32_t milli_bpm);
  void reset();

  void on_onset(int64_t t_us, float strength);

  // True while onsets are arriving (no gap beyond 4× the last classified
  // IOI, min 2 s). Going inactive drops to kIdle.
  bool active(int64_t now_us);

  Lock lock_state() const { return lock_; }

  // One-shot, hysteretic, integer-BPM, rate-limited. 0 until locked.
  bool take_tempo_update(uint32_t* milli_bpm);

  uint32_t tempo_milli_bpm() const { return published_mbpm_; }
  uint32_t estimate_milli_bpm() const { return est_.tempo_milli_bpm(); }
  uint8_t subdivision() const { return have_class_ ? subdiv_ : 0; }
  uint32_t onset_count() const { return onset_count_; }
  uint32_t rejected_count() const { return rejected_count_; }

 private:
  enum class IoiClass : uint8_t {
    kNone = 0,
    kT = 1,
    kT2 = 2,
    kT4 = 3,
    k2T = 4,
    k4T = 5,
  };

  uint64_t beat_period_us() const;
  IoiClass classify(int64_t dt) const;
  uint8_t ppqn_for(IoiClass c) const;
  void apply_class(IoiClass c);
  void feed(IoiClass c, int64_t t_us, int64_t dt);
  void feed_pulse(int64_t t_us);
  void reset_estimator();
  void enter_idle();
  bool in_window(uint32_t mbpm) const;
  uint32_t prior_mbpm() const;
  static uint32_t integer_mbpm(uint32_t mbpm);

  ExtClockEstimator est_;
  uint32_t session_mbpm_ = 0;
  Lock lock_ = Lock::kIdle;
  IoiClass class_ = IoiClass::kNone;
  IoiClass vote_class_ = IoiClass::kNone;
  uint32_t vote_streak_ = 0;
  uint8_t subdiv_ = 0;
  bool have_class_ = false;

  int64_t last_onset_us_ = INT64_MIN;
  bool have_onset_ = false;
  int64_t last_fed_us_ = INT64_MIN;
  bool have_fed_ = false;

  uint32_t classified_iois_ = 0;
  int64_t last_classified_ioi_us_ = 0;
  uint32_t onset_count_ = 0;
  uint32_t rejected_count_ = 0;

  uint32_t published_mbpm_ = 0;
  int64_t last_publish_us_ = INT64_MIN;
  int64_t last_event_us_ = 0;
};

}  // namespace neon
