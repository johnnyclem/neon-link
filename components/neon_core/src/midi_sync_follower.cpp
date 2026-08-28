#include "neon/midi/sync_follower.hpp"

namespace neon {
namespace midi {

void SyncFollower::on_event(const SyncEvent& ev) {
  pll_.set_transport(ev.transport);
  switch (ev.kind) {
    case SyncEvent::Kind::kTick:
      pll_.on_tick(ev.t_us);
      break;
    case SyncEvent::Kind::kStart:
      pll_.on_start(ev.t_us);
      break;
    case SyncEvent::Kind::kContinue:
      pll_.on_continue(ev.t_us);
      break;
    case SyncEvent::Kind::kStop:
      pll_.on_stop(ev.t_us);
      break;
    case SyncEvent::Kind::kSpp:
      pll_.on_spp(ev.spp);
      break;
  }
}

SyncFollower::Actions SyncFollower::poll(int64_t now_us, bool allowed) {
  Actions a;
  const bool live = allowed && pll_.active(now_us) && pll_.valid();
  if (!live) {
    if (following_) {
      // Handover or loss: forget what was published so a later re-follow
      // republishes from scratch. A pending downbeat is deliberately
      // kept — anchoring to a Start that fired while something else held
      // the clock is still correct once MIDI takes over (a past downbeat
      // time is a legal phase anchor), and a downbeat from a clock that
      // actually died is cleared with the PLL's inactivity reset.
      following_ = false;
      published_mbpm_ = 0;
      have_playing_ = false;
    }
    return a;
  }

  following_ = true;
  a.following = true;

  const uint32_t mbpm = pll_.tempo_milli_bpm();
  if (published_mbpm_ == 0) {
    a.set_tempo = true;
    a.tempo_mbpm = mbpm;
    published_mbpm_ = mbpm;
    last_tempo_us_ = now_us;
  } else {
    const int64_t diff = static_cast<int64_t>(mbpm) -
                         static_cast<int64_t>(published_mbpm_);
    const int64_t abs_diff = diff < 0 ? -diff : diff;
    if (abs_diff * kHysteresisDen > published_mbpm_ &&
        now_us - last_tempo_us_ >= kTempoGapUs) {
      a.set_tempo = true;
      a.tempo_mbpm = mbpm;
      published_mbpm_ = mbpm;
      last_tempo_us_ = now_us;
    }
  }

  int64_t downbeat = 0;
  if (pll_.take_downbeat(&downbeat)) {
    a.anchor_downbeat = true;
    a.downbeat_us = downbeat;
  }

  const bool playing = pll_.playing();
  if (!have_playing_) {
    // First poll while following: propagate a running transport, but a
    // merely free-running clock must not stop a playing session.
    have_playing_ = true;
    sent_playing_ = playing;
    if (playing) {
      a.set_playing = true;
      a.playing = true;
    }
  } else if (playing != sent_playing_) {
    sent_playing_ = playing;
    a.set_playing = true;
    a.playing = playing;
  }

  return a;
}

}  // namespace midi
}  // namespace neon
