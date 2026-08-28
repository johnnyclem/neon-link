#include "neon/midi/sync_follower.hpp"

namespace neon {
namespace midi {

void SyncFollower::on_event(const SyncEvent& ev) {
  int64_t t_us = ev.t_us;
  MidiClockPll::Transport transport = ev.transport;
  if (ev.transport == MidiClockPll::Transport::kBle) {
    if (ev.kind == SyncEvent::Kind::kTick && ev.sender_ms13 != kNoSenderMs) {
      t_us = ble_map_.on_tick(ev.t_us, ev.sender_ms13);
    }
    if (ble_map_.trusted()) {
      // Decoded sender stamps recover ±1 ms spacing — USB-grade timing,
      // so run the kUsb loop instead of BLE's burst-hardened one. The
      // handful of raw-arrival ticks still in the PLL's window at the
      // switch show up as one clamped residual, inside the loop's
      // pull-in range.
      transport = MidiClockPll::Transport::kUsb;
    }
  }
  pll_.set_transport(transport);
  switch (ev.kind) {
    case SyncEvent::Kind::kTick:
      pll_.on_tick(t_us);
      break;
    case SyncEvent::Kind::kStart:
      pll_.on_start();
      break;
    case SyncEvent::Kind::kContinue:
      pll_.on_continue();
      break;
    case SyncEvent::Kind::kStop:
      pll_.on_stop();
      break;
    case SyncEvent::Kind::kSpp:
      pll_.on_spp(ev.spp);
      break;
  }
}

SyncFollower::Actions SyncFollower::poll(int64_t now_us, bool allowed,
                                         const SessionView& session) {
  Actions a;
  // require_peer holds writes exactly like an arbiter veto: silent warm
  // tracking, then a from-scratch republish (pending downbeat included)
  // once a peer shows up. Only meaningful when the caller supplies
  // session state — without it there is no peer count to trust.
  const bool peer_gated = require_peer_ && session.valid && session.peers == 0;
  const bool live =
      allowed && !peer_gated && pll_.active(now_us) && pll_.valid();
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

  const bool playing = pll_.playing();
  const uint32_t mbpm = pll_.tempo_milli_bpm();
  if (published_mbpm_ == 0) {
    a.set_tempo = true;
    a.tempo_mbpm = mbpm;
    published_mbpm_ = mbpm;
    last_tempo_us_ = now_us;
  } else {
    // Authority policy (spike §8.4, DawFollower's shape): while the MIDI
    // transport runs the sender owns the tempo, so the comparison is
    // against the *session's* value — a peer edit reads as divergence
    // and is corrected. While stopped (or with no session view) the
    // reference is our own last publish, so only a genuine MIDI tempo
    // move writes and peer edits stand in between. Both directions ride
    // the same hysteresis band and rate limit.
    const uint32_t ref = (playing && session.valid) ? session.tempo_mbpm
                                                    : published_mbpm_;
    const int64_t diff =
        static_cast<int64_t>(mbpm) - static_cast<int64_t>(ref);
    const int64_t abs_diff = diff < 0 ? -diff : diff;
    if (abs_diff * kHysteresisDen > ref &&
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
  } else if (playing && session.valid && !session.playing &&
             now_us - last_hold_us_ >= kTempoGapUs) {
    // Level-assert the transport while the sender runs: a peer stopping
    // the session under a running MIDI transport is corrected,
    // rate-limited. The stopped direction stays edges-only — while the
    // MIDI transport is stopped, peers are free to play.
    a.set_playing = true;
    a.playing = true;
    last_hold_us_ = now_us;
  }

  return a;
}

}  // namespace midi
}  // namespace neon
