#include "link_session_daisy.h"

#include "session_daisy.h"

#include "neon/transport.hpp"

#include "timebase_daisy.h"

namespace tsession {

void TimelineSession::start(double initial_bpm) {
  if (started_) {
    return;
  }
  const int64_t now = daisy_now_us();
  tl_.init(neon::milli_bpm_from_bpm(initial_bpm),
           tl_.snapshot().quantum_beats, now);
  play_flag_ = tl_.playing();
  play_at_us_ = now;
  started_ = true;
}

bool TimelineSession::capture(hal::LinkState& out) {
  if (!started_) {
    return false;
  }
  const int64_t now = daisy_now_us();
  const bool due = neon::playing_at(play_flag_, play_at_us_, now);
  if ((tl_.playing() != 0) != due) {
    tl_.set_playing(due, now);
  }
  const neon::TimelineSnapshot& tl = tl_.snapshot();
  out.tempo_bpm = static_cast<double>(tl_.milli_bpm()) / 1000.0;
  out.beat_at_origin =
      static_cast<double>(neon::beat_at_q32(tl, now)) / 4294967296.0;
  out.origin_us = now;
  out.quantum = static_cast<double>(tl.quantum_beats);
  out.playing = due;
  out.num_peers = 0;  // always: there is no session to have peers in
  return true;
}

void TimelineSession::set_tempo(double bpm) {
  tl_.set_tempo(neon::milli_bpm_from_bpm(bpm), daisy_now_us());
}

void TimelineSession::set_playing(bool playing, int64_t at_us) {
  play_flag_ = playing;
  play_at_us_ = at_us >= 0 ? at_us : daisy_now_us();
  if (play_at_us_ <= daisy_now_us()) {
    tl_.set_playing(playing, play_at_us_);
  }
}

void TimelineSession::request_beat_at_time(int64_t t_us) {
  tl_.anchor_downbeat(t_us);
}

void TimelineSession::set_start_stop_sync(bool enable) {
  (void)enable;  // meaningful only with peers; honest no-op here
}

void TimelineSession::set_quantum(double beats) {
  const uint32_t q = beats > 0.0 ? static_cast<uint32_t>(beats + 0.5) : 4;
  if (q != tl_.snapshot().quantum_beats) {
    tl_.set_quantum(q, daisy_now_us());
  }
}

TimelineSession& session() {
  static TimelineSession s;
  return s;
}

}  // namespace tsession

hal::ILinkSession& daisy_session() { return tsession::session(); }
