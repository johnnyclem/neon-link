#include "neon/audio/tempo_follower.hpp"

#include "neon/config/model.hpp"
#include "neon/transport.hpp"

namespace neon {

void AudioTempoFollower::set_session_tempo(uint32_t milli_bpm) {
  session_mbpm_ = milli_bpm;
}

void AudioTempoFollower::reset() {
  const uint32_t p = est_.input_ppqn();
  est_.set_input_ppqn(p == 1u ? 2u : 1u);
  est_.set_input_ppqn(p);
  session_mbpm_ = 0;
  lock_ = Lock::kIdle;
  class_ = IoiClass::kNone;
  vote_class_ = IoiClass::kNone;
  vote_streak_ = 0;
  subdiv_ = 0;
  have_class_ = false;
  last_onset_us_ = INT64_MIN;
  have_onset_ = false;
  last_fed_us_ = INT64_MIN;
  have_fed_ = false;
  classified_iois_ = 0;
  onset_count_ = 0;
  rejected_count_ = 0;
  published_mbpm_ = 0;
  last_publish_us_ = INT64_MIN;
  last_event_us_ = 0;
}

uint64_t AudioTempoFollower::beat_period_us() const {
  const uint32_t mbpm = prior_mbpm();
  return 60000000000ull / static_cast<uint64_t>(mbpm);
}

uint32_t AudioTempoFollower::prior_mbpm() const {
  return session_mbpm_ != 0 ? session_mbpm_ : 120000u;
}

AudioTempoFollower::IoiClass AudioTempoFollower::classify(int64_t dt) const {
  if (dt <= 0) {
    return IoiClass::kNone;
  }
  const uint64_t T = beat_period_us();
  const uint64_t u = static_cast<uint64_t>(dt);
  auto in_win = [](uint64_t x, uint64_t center) {
    if (center == 0) {
      return false;
    }
    return x * 100ull >= 88ull * center && x * 100ull <= 112ull * center;
  };
  if (in_win(u, T)) {
    return IoiClass::kT;
  }
  if (in_win(u, T / 2ull)) {
    return IoiClass::kT2;
  }
  if (in_win(u, T / 4ull)) {
    return IoiClass::kT4;
  }
  if (in_win(u, T * 2ull)) {
    return IoiClass::k2T;
  }
  if (in_win(u, T * 4ull)) {
    return IoiClass::k4T;
  }
  return IoiClass::kNone;
}

uint8_t AudioTempoFollower::ppqn_for(IoiClass c) const {
  switch (c) {
    case IoiClass::kT2:
      return 2;
    case IoiClass::kT4:
      return 4;
    case IoiClass::kT:
    case IoiClass::k2T:
    case IoiClass::k4T:
      return 1;
    case IoiClass::kNone:
    default:
      return 0;
  }
}

void AudioTempoFollower::apply_class(IoiClass c) {
  class_ = c;
  have_class_ = true;
  const uint8_t p = ppqn_for(c);
  subdiv_ = p;
  const uint32_t want = p != 0 ? p : 1u;
  // T and 2T share ppqn=1; force a history reset on any class change.
  if (est_.input_ppqn() == want) {
    est_.set_input_ppqn(want == 1u ? 2u : 1u);
  }
  est_.set_input_ppqn(want);
  last_fed_us_ = INT64_MIN;
  have_fed_ = false;
}

void AudioTempoFollower::feed_pulse(int64_t t_us) {
  if (have_fed_ && t_us <= last_fed_us_) {
    return;
  }
  est_.on_pulse(t_us);
  last_fed_us_ = t_us;
  have_fed_ = true;
}

void AudioTempoFollower::feed(IoiClass c, int64_t t_us, int64_t dt) {
  switch (c) {
    case IoiClass::k2T:
      feed_pulse(t_us - dt / 2);
      feed_pulse(t_us);
      break;
    case IoiClass::k4T:
      feed_pulse(t_us - (3 * dt) / 4);
      feed_pulse(t_us - dt / 2);
      feed_pulse(t_us - dt / 4);
      feed_pulse(t_us);
      break;
    case IoiClass::kT:
    case IoiClass::kT2:
    case IoiClass::kT4:
      feed_pulse(t_us);
      break;
    case IoiClass::kNone:
    default:
      break;
  }
}

void AudioTempoFollower::enter_idle() {
  lock_ = Lock::kIdle;
  class_ = IoiClass::kNone;
  vote_class_ = IoiClass::kNone;
  vote_streak_ = 0;
  subdiv_ = 0;
  have_class_ = false;
  classified_iois_ = 0;
  last_onset_us_ = INT64_MIN;
  have_onset_ = false;
  last_fed_us_ = INT64_MIN;
  have_fed_ = false;
  published_mbpm_ = 0;
  last_publish_us_ = INT64_MIN;
}

void AudioTempoFollower::on_onset(int64_t t_us, float strength) {
  (void)strength;
  ++onset_count_;
  last_event_us_ = t_us;

  if (!have_onset_) {
    last_onset_us_ = t_us;
    have_onset_ = true;
    return;
  }

  const int64_t dt = t_us - last_onset_us_;
  last_onset_us_ = t_us;
  const IoiClass c = classify(dt);
  if (c == IoiClass::kNone) {
    ++rejected_count_;
    vote_streak_ = 0;
    vote_class_ = IoiClass::kNone;
    return;
  }

  bool accept = false;
  if (!have_class_) {
    apply_class(c);
    vote_class_ = c;
    vote_streak_ = 1;
    accept = true;
  } else if (c == class_) {
    vote_class_ = c;
    vote_streak_ = 1;
    accept = true;
  } else {
    if (c == vote_class_) {
      ++vote_streak_;
    } else {
      vote_class_ = c;
      vote_streak_ = 1;
    }
    if (vote_streak_ >= kVoteRun) {
      apply_class(c);
      vote_streak_ = 1;
      accept = true;
    }
  }

  if (!accept) {
    return;
  }

  feed(c, t_us, dt);
  ++classified_iois_;
  if (lock_ == Lock::kIdle) {
    lock_ = Lock::kAcquiring;
  }
}

bool AudioTempoFollower::active(int64_t now_us) {
  if (!est_.active(now_us)) {
    if (lock_ != Lock::kIdle) {
      enter_idle();
    }
    return false;
  }
  return lock_ != Lock::kIdle;
}

uint32_t AudioTempoFollower::integer_mbpm(uint32_t mbpm) {
  if (mbpm < 500) {
    return 0;
  }
  return ((mbpm + 500u) / 1000u) * 1000u;
}

bool AudioTempoFollower::in_window(uint32_t mbpm) const {
  const uint32_t session = prior_mbpm();
  const int64_t diff = static_cast<int64_t>(mbpm) - static_cast<int64_t>(session);
  const int64_t abs_diff = diff < 0 ? -diff : diff;
  if (lock_ != Lock::kLocked) {
    return abs_diff * 100 <= static_cast<int64_t>(session) * 12;
  }
  int64_t w = static_cast<int64_t>(session) * 5 / 100;
  if (w < 6000) {
    w = 6000;
  }
  return abs_diff <= w;
}

bool AudioTempoFollower::take_tempo_update(uint32_t* milli_bpm) {
  uint32_t scratch = 0;
  (void)est_.take_tempo_update(&scratch);

  if (lock_ == Lock::kIdle || classified_iois_ < kMinIoIs) {
    return false;
  }
  const uint32_t est = est_.tempo_milli_bpm();
  if (est == 0) {
    return false;
  }

  const uint32_t snapped = integer_mbpm(est);
  const uint32_t clamped = clamp_milli_bpm(static_cast<int64_t>(snapped));
  if (clamped < kMinMilliBpm || clamped > kMaxMilliBpm) {
    return false;
  }
  if (!in_window(clamped)) {
    return false;
  }

  if (published_mbpm_ == 0) {
    published_mbpm_ = clamped;
    last_publish_us_ = last_event_us_;
    lock_ = Lock::kLocked;
    *milli_bpm = clamped;
    return true;
  }

  const int64_t raw_diff =
      static_cast<int64_t>(est) - static_cast<int64_t>(published_mbpm_);
  const int64_t abs_raw = raw_diff < 0 ? -raw_diff : raw_diff;
  if (clamped == published_mbpm_ ||
      abs_raw < static_cast<int64_t>(kIntegerGuardMbp)) {
    return false;
  }
  if (last_event_us_ - last_publish_us_ < kTempoGapUs) {
    return false;
  }

  int64_t delta =
      static_cast<int64_t>(clamped) - static_cast<int64_t>(published_mbpm_);
  uint32_t out = clamped;
  if (delta > static_cast<int64_t>(kSlewMbp)) {
    out = published_mbpm_ + kSlewMbp;
  } else if (delta < -static_cast<int64_t>(kSlewMbp)) {
    out = published_mbpm_ - kSlewMbp;
  }
  out = clamp_milli_bpm(static_cast<int64_t>(out));
  if (out == published_mbpm_) {
    return false;
  }

  published_mbpm_ = out;
  last_publish_us_ = last_event_us_;
  *milli_bpm = out;
  return true;
}

}  // namespace neon
