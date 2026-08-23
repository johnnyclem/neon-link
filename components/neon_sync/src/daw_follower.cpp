#include "nsync/daw_follower.hpp"

#include <cmath>

namespace nsync {

namespace {

// Session beat minus DAW beat, wrapped to the nearest quantum image so a
// grid that is one whole loop apart still reads as aligned.
double wrap_half(double d, double q) {
  double w = std::fmod(d, q);
  if (w >= q / 2.0) {
    w -= q;
  } else if (w < -q / 2.0) {
    w += q;
  }
  return w;
}

int64_t round_i64(double v) {
  return static_cast<int64_t>(v >= 0.0 ? v + 0.5 : v - 0.5);
}

}  // namespace

void DawFollower::set_drive(bool enabled) {
  if (enabled == drive_) {
    return;
  }
  drive_ = enabled;
  // Re-baseline so flipping the switch never manufactures an edge out of
  // history recorded under the other mode.
  have_last_ = false;
  phase_strikes_ = 0;
  phase_valid_ = false;
  phase_locked_ = false;
}

bool DawFollower::can_correct(int64_t now_us) const {
  return !have_correction_ ||
         now_us - last_correction_us_ >= cfg_.correction_gap_us;
}

void DawFollower::mark_correction(int64_t now_us) {
  have_correction_ = true;
  last_correction_us_ = now_us;
}

void DawFollower::update(const DawPlayhead& ph, int64_t now_us) {
  if (!node_.started()) {
    return;
  }
  const int64_t age = now_us - ph.sampled_us;
  const bool fresh = ph.valid && age >= 0 && age <= cfg_.stale_after_us;

  daw_fresh_ = fresh;
  daw_playing_ = fresh && ph.playing;
  if (fresh && ph.bpm > 0.0) {
    daw_bpm_ = ph.bpm;
  }

  if (!fresh) {
    have_last_ = false;
    phase_strikes_ = 0;
    phase_valid_ = false;
    phase_locked_ = false;
    return;
  }

  hal::LinkState st;
  if (!node_.capture(st, now_us)) {
    return;
  }
  const bool tempo_known = ph.bpm > 0.0;
  const bool may_write = drive_ && (!cfg_.require_peer || st.num_peers > 0);

  // Transport edges write immediately, both directions.
  if (may_write && have_last_ && ph.playing != last_playing_) {
    if (ph.playing) {
      if (tempo_known) {
        node_.set_tempo(ph.bpm, now_us);
        node_.capture(st, now_us);
        align_grid(ph, st, now_us);
      }
      node_.set_playing(true, now_us);
      mark_correction(now_us);
      phase_strikes_ = 0;
    } else {
      node_.set_playing(false, now_us);
      mark_correction(now_us);
    }
    node_.capture(st, now_us);
  }

  if (may_write && ph.playing) {
    // The DAW is authoritative while its transport runs: hold the mesh
    // tempo and transport against it, rate-limited.
    if (tempo_known &&
        std::fabs(st.tempo_bpm - ph.bpm) > cfg_.tempo_epsilon_bpm &&
        can_correct(now_us)) {
      node_.set_tempo(ph.bpm, now_us);
      mark_correction(now_us);
      node_.capture(st, now_us);
    }
    if (!st.playing && can_correct(now_us)) {
      node_.set_playing(true, now_us);
      mark_correction(now_us);
      node_.capture(st, now_us);
    }
  } else if (may_write && tempo_known && have_last_ &&
             std::fabs(ph.bpm - last_daw_bpm_) > cfg_.tempo_epsilon_bpm) {
    // Stopped: a tempo edit in the DAW still previews onto the mesh, but
    // only as an edge, so mesh-side edits stand between DAW changes.
    node_.set_tempo(ph.bpm, now_us);
    mark_correction(now_us);
    node_.capture(st, now_us);
  }

  // Phase only means anything while both grids run at one tempo.
  phase_valid_ = false;
  if (ph.playing && tempo_known &&
      std::fabs(st.tempo_bpm - ph.bpm) <= cfg_.phase_tempo_window_bpm) {
    int64_t err = 0;
    if (phase_error(ph, st, now_us, &err)) {
      phase_valid_ = true;
      phase_err_us_ = err;
      const bool inside = err <= cfg_.phase_tolerance_us &&
                          err >= -cfg_.phase_tolerance_us;
      phase_locked_ = inside;
      if (inside) {
        phase_strikes_ = 0;
      } else if (may_write) {
        ++phase_strikes_;
        if (phase_strikes_ >= cfg_.phase_debounce && can_correct(now_us)) {
          align_grid(ph, st, now_us);
          mark_correction(now_us);
          phase_strikes_ = 0;
        }
      }
    }
  } else {
    phase_strikes_ = 0;
    phase_locked_ = false;
  }

  have_last_ = true;
  last_playing_ = ph.playing;
  if (tempo_known) {
    last_daw_bpm_ = ph.bpm;
  }
}

bool DawFollower::phase_error(const DawPlayhead& ph, const hal::LinkState& st,
                              int64_t now_us, int64_t* err_us) const {
  if (ph.bpm <= 0.0 || st.tempo_bpm <= 0.0) {
    return false;
  }
  const double q = st.quantum > 0.0 ? st.quantum : 4.0;
  const double daw_mpb = 60000000.0 / ph.bpm;
  // Project the sample to now on the DAW's own grid; st.beat_at_origin is
  // already the session beat at now (capture origin == now_us).
  const double daw_beat =
      ph.beat + static_cast<double>(now_us - ph.sampled_us) / daw_mpb;
  const double err_beats = wrap_half(st.beat_at_origin - daw_beat, q);
  *err_us = round_i64(err_beats * (60000000.0 / st.tempo_bpm));
  return true;
}

void DawFollower::align_grid(const DawPlayhead& ph, const hal::LinkState& st,
                             int64_t now_us) {
  if (ph.bpm <= 0.0) {
    return;
  }
  const double q = st.quantum > 0.0 ? st.quantum : 4.0;
  const double daw_mpb = 60000000.0 / ph.bpm;
  // The DAW's most recent bar line (quantum boundary in PPQ), on our clock.
  const double boundary = std::floor(ph.beat / q) * q;
  const int64_t t_boundary =
      ph.sampled_us + round_i64((boundary - ph.beat) * daw_mpb);
  node_.request_beat_at_time(t_boundary, now_us);
}

FollowerStatus DawFollower::status(int64_t now_us) const {
  FollowerStatus s;
  s.drive = drive_;
  hal::LinkState st;
  if (node_.capture(st, now_us)) {
    s.session_up = true;
    s.session_bpm = st.tempo_bpm;
    s.session_playing = st.playing;
    s.peers = st.num_peers;
    s.quantum = st.quantum;
    s.session_id = node_.session_id();
  }
  s.daw_fresh = daw_fresh_;
  s.daw_playing = daw_playing_;
  s.daw_bpm = daw_bpm_;
  s.phase_valid = phase_valid_;
  s.phase_err_us = phase_err_us_;
  s.phase_locked = phase_locked_;
  return s;
}

}  // namespace nsync
