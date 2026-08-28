#include "neon/midi/clock_pll.hpp"

#include "neon/fixed_math.hpp"

namespace neon {

namespace {

int64_t clamp_i64(int64_t v, int64_t lo, int64_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// floor((ticks << 32) / 24) with correct floor for negatives.
int64_t beat_q32_from_song_ticks(int64_t ticks) {
  if (ticks >= 0) {
    return static_cast<int64_t>((static_cast<uint64_t>(ticks) << 32) / 24u);
  }
  const uint64_t mag = static_cast<uint64_t>(-ticks) << 32;
  int64_t q = static_cast<int64_t>(mag / 24u);
  if (mag % 24u != 0) {
    ++q;
  }
  return -q;
}

}  // namespace

MidiClockPll::Gains MidiClockPll::gains() const {
  // beta ~ alpha^2/4 keeps the phase/rate pair near critical damping;
  // noisier transports get slower loops, and BLE's burst delivery gets a
  // longer step debounce so bundled packets never read as a tempo step.
  switch (transport_) {
    case Transport::kUsb:
      return Gains{5, 12, 8};
    case Transport::kBle:
      return Gains{6, 14, 16};
    case Transport::kDin:
    default:
      return Gains{4, 10, 6};
  }
}

void MidiClockPll::reset() {
  time_count_ = 0;
  time_next_ = 0;
  last_tick_us_ = INT64_MIN;
  have_rate_ = false;
  anchor_tick_ = 0;
  anchor_us_ = 0;
  us_per_tick_q32_ = 0;
  pll_tick_ = -1;
  residual_us_ = 0;
  ticks_ = 0;
  locked_ = false;
  lock_streak_ = 0;
  step_streak_ = 0;
  playing_ = false;
  beat_anchored_ = false;
  song_offset_ = 0;
  resume_song_tick_ = 0;
  pending_start_ = false;
  pending_continue_ = false;
  downbeat_pending_ = false;
  downbeat_us_ = 0;
}

// Least-squares slope of the window's timestamps against their (implicit,
// consecutive) tick indices: the unbiased tick-period estimate. A period
// median would be poisoned by BLE bursts — several ticks on one arrival
// time make the surviving positive periods multiples of the true period —
// while the regression sees through them, and a single delayed DIN byte
// shifts the slope by well under the loop's pull-in range.
int64_t MidiClockPll::window_slope_us() const {
  const uint32_t n = time_count_;
  if (n < 2) {
    return 0;
  }
  // Ring -> oldest-first order.
  const uint32_t start = n == kWindow ? time_next_ : 0;
  int64_t num = 0;   // sum of (2i - (n-1)) * t_i, centered so sums cancel
  int64_t den = 0;   // sum of (2i - (n-1))^2
  for (uint32_t i = 0; i < n; ++i) {
    const int64_t c = 2 * static_cast<int64_t>(i) - (n - 1);
    num += c * times_[(start + i) % kWindow];
    den += c * c;
  }
  // slope = num / (den/2); den is even for every n.
  return (2 * num) / den;
}

bool MidiClockPll::seed_from_window(int64_t t_us) {
  if (time_count_ < kWindow) {
    return false;
  }
  const int64_t slope =
      clamp_i64(window_slope_us(), kMinTickUs, kMaxTickUs);
  us_per_tick_q32_ = static_cast<uint64_t>(slope) << 32;
  anchor_tick_ = pll_tick_;
  anchor_us_ = t_us;
  have_rate_ = true;
  locked_ = false;
  lock_streak_ = 0;
  step_streak_ = 0;
  return true;
}

int64_t MidiClockPll::period_us() const {
  return have_rate_ ? static_cast<int64_t>(us_per_tick_q32_ >> 32) : 0;
}

int64_t MidiClockPll::predict(int64_t tick) const {
  const int64_t d = tick - anchor_tick_;
  // Split the Q32.32 multiply so the intermediate stays in int64 (the
  // extrapolation distance is at most a few ticks between anchors).
  const int64_t whole = static_cast<int64_t>(us_per_tick_q32_ >> 32);
  const int64_t frac = static_cast<int64_t>(us_per_tick_q32_ & 0xffffffffull);
  return anchor_us_ + d * whole + ((d * frac) >> 32);
}

void MidiClockPll::apply_transport_pending(int64_t t_us) {
  if (pending_start_) {
    pending_start_ = false;
    pending_continue_ = false;
    song_offset_ = -pll_tick_;
    playing_ = true;
    beat_anchored_ = true;
    downbeat_pending_ = true;
    // The filtered time of this tick when the servo has one (the anchor
    // sits on this tick by the time pendings apply) — the raw timestamp
    // otherwise, since nothing better exists yet.
    downbeat_us_ = have_rate_ ? anchor_us_ : t_us;
  } else if (pending_continue_) {
    pending_continue_ = false;
    song_offset_ = resume_song_tick_ - pll_tick_;
    playing_ = true;
    beat_anchored_ = true;
  }
}

void MidiClockPll::on_tick(int64_t t_us) {
  if (ticks_ < 0xffffffffu) {
    ++ticks_;
  }

  if (last_tick_us_ == INT64_MIN) {
    pll_tick_ = 0;
    last_tick_us_ = t_us;
    times_[time_next_] = t_us;
    time_next_ = (time_next_ + 1) % kWindow;
    time_count_ = 1;
    apply_transport_pending(t_us);
    return;
  }

  const int64_t period = t_us - last_tick_us_;
  if (period > kMaxPeriodUs) {
    // The sender stalled long past any musical tempo: the grid on the
    // other side is gone, so restart from scratch. (active() catches the
    // same condition between ticks.)
    reset();
    pll_tick_ = 0;
    last_tick_us_ = t_us;
    times_[0] = t_us;
    time_next_ = 1;
    time_count_ = 1;
    ticks_ = 1;
    return;
  }
  last_tick_us_ = t_us;
  ++pll_tick_;

  // Every 0xF8 is a real tick — the count is authoritative and never
  // skips, and every timestamp (BLE burst duplicates included) enters the
  // regression window.
  times_[time_next_] = t_us;
  time_next_ = (time_next_ + 1) % kWindow;
  if (time_count_ < kWindow) {
    ++time_count_;
  }

  if (!have_rate_) {
    seed_from_window(t_us);
    apply_transport_pending(t_us);
    return;
  }

  const Gains g = gains();
  const int64_t tick_us = period_us();
  const int64_t e_raw = t_us - predict(pll_tick_);
  residual_us_ = e_raw;

  // A sustained error beyond half a tick is a tempo step, not jitter:
  // reseed the rate from the window (which by now holds the new tempo)
  // and snap the anchor. The tick count — and with it the musical
  // position — carries straight through.
  if (e_raw > tick_us / 2 || e_raw < -tick_us / 2) {
    if (++step_streak_ >= g.step_ticks) {
      seed_from_window(t_us);
      apply_transport_pending(t_us);
      return;
    }
  } else {
    step_streak_ = 0;
  }

  const int64_t e = clamp_i64(e_raw, -tick_us / 4, tick_us / 4);

  // Phase: re-anchor on this tick, pulled a fraction toward the
  // observation. Re-anchoring every tick keeps extrapolation distances
  // (and the multiplies) small.
  anchor_us_ = (t_us - e_raw) + (e >> g.phase_shift);
  anchor_tick_ = pll_tick_;

  // Rate: integral term. |e| <= T/4 bounds the per-tick change to a few
  // hundred ppm, so jitter cannot yank the estimate, but a smooth tempo
  // ramp is followed with constant lag instead of accumulating error.
  const int64_t rate_delta = e << (32 - g.rate_shift);
  int64_t next_rate = static_cast<int64_t>(us_per_tick_q32_) + rate_delta;
  next_rate = clamp_i64(next_rate, kMinTickUs << 32, kMaxTickUs << 32);
  us_per_tick_q32_ = static_cast<uint64_t>(next_rate);

  const int64_t abs_e = e_raw < 0 ? -e_raw : e_raw;
  if (abs_e < tick_us / 8) {
    if (lock_streak_ < 0xffffffffu) {
      ++lock_streak_;
    }
  } else {
    lock_streak_ = 0;
  }
  locked_ = lock_streak_ >= kLockTicks;

  apply_transport_pending(t_us);
}

void MidiClockPll::on_start(int64_t) {
  pending_start_ = true;
  pending_continue_ = false;
  resume_song_tick_ = 0;
}

void MidiClockPll::on_continue(int64_t) {
  if (!playing_) {
    pending_continue_ = true;
  }
}

void MidiClockPll::on_stop(int64_t) {
  pending_start_ = false;
  pending_continue_ = false;
  if (playing_) {
    playing_ = false;
    // The position is frozen, not live: a free-running clock must not
    // advance it. The next tick after a Continue lands on the position
    // that would have come next.
    beat_anchored_ = false;
    resume_song_tick_ = pll_tick_ + song_offset_ + 1;
  }
}

void MidiClockPll::on_spp(uint16_t sixteenths) {
  resume_song_tick_ = static_cast<int64_t>(sixteenths) * 6;
  if (playing_) {
    // A position jump mid-play: the next tick lands on the new position.
    song_offset_ = resume_song_tick_ - (pll_tick_ + 1);
  }
}

bool MidiClockPll::active(int64_t now_us) {
  if (pll_tick_ < 0) {
    return false;
  }
  int64_t timeout = kMinTimeoutUs;
  const int64_t expected = period_us();
  if (expected > 0 && expected * 8 > timeout) {
    timeout = expected * 8;
  }
  if (now_us - last_tick_us_ > timeout) {
    reset();
    return false;
  }
  return true;
}

bool MidiClockPll::model(Model* out) const {
  if (!have_rate_) {
    return false;
  }
  out->tempo_mpb_q32 = us_per_tick_q32_ * 24u;
  out->origin_us = anchor_us_;
  out->playing = playing_;
  out->beat_valid = beat_anchored_;
  out->beat_at_origin_q32 =
      beat_anchored_ ? beat_q32_from_song_ticks(anchor_tick_ + song_offset_)
                     : 0;
  return true;
}

uint32_t MidiClockPll::tempo_milli_bpm() const {
  if (!have_rate_) {
    return 0;
  }
  const uint64_t mpb_q32 = us_per_tick_q32_ * 24u;
  const uint64_t mbpm =
      div_u128_u64(mul_u64(60000000000ull, 1ull << 32), mpb_q32);
  if (mbpm < 1000) {
    return 1000;
  }
  if (mbpm > 999000) {
    return 999000;
  }
  return static_cast<uint32_t>(mbpm);
}

bool MidiClockPll::take_downbeat(int64_t* t_us) {
  if (!downbeat_pending_) {
    return false;
  }
  downbeat_pending_ = false;
  *t_us = downbeat_us_;
  return true;
}

}  // namespace neon
