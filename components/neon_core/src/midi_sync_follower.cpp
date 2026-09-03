#include "neon/midi/sync_follower.hpp"

#include <cmath>

namespace neon {
namespace midi {

namespace {

uint32_t integer_mbpm(uint32_t mbpm) {
  if (mbpm < 500) {
    return 1000;
  }
  return ((mbpm + 500u) / 1000u) * 1000u;
}

// Hold the last published integer until the estimate has walked 0.6 BPM
// past it — stops 127.4 / 128.1 chatter from becoming session writes.
uint32_t snap_integer_mbpm(uint32_t mbpm, uint32_t held) {
  const uint32_t snapped = integer_mbpm(mbpm);
  if (held == 0) {
    return snapped;
  }
  const int64_t diff =
      static_cast<int64_t>(mbpm) - static_cast<int64_t>(held);
  const int64_t abs_diff = diff < 0 ? -diff : diff;
  if (snapped != held &&
      abs_diff >= static_cast<int64_t>(SyncFollower::kIntegerGuardMbp)) {
    return snapped;
  }
  return held;
}

}  // namespace

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
      pending_mbpm_ = 0;
      have_playing_ = false;
      last_phase_us_ = 0;
    }
    return a;
  }

  following_ = true;
  a.following = true;

  const bool playing = pll_.playing();
  const uint32_t mbpm =
      snap_integer_mbpm(pll_.tempo_milli_bpm(), published_mbpm_);
  if (mbpm != pending_mbpm_) {
    pending_mbpm_ = mbpm;
    pending_since_us_ = now_us;
  }
  // Never write a unlocked/hunting estimate into the session — that is
  // the 140→90→130 OLED flicker under UART+WiFi+peer load. Wait until
  // the PLL has a beat of small residual, then dwell on one integer.
  const bool dwelt = now_us - pending_since_us_ >= kIntegerDwellUs;
  if (pll_.locked() && published_mbpm_ == 0) {
    a.set_tempo = true;
    a.tempo_mbpm = mbpm;
    published_mbpm_ = mbpm;
    last_tempo_us_ = now_us;
  } else if (pll_.locked() && dwelt) {
    const uint32_t ref = integer_mbpm(
        (playing && session.valid) ? session.tempo_mbpm : published_mbpm_);
    if (mbpm != ref && now_us - last_tempo_us_ >= kTempoGapUs) {
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
    last_phase_us_ = now_us;
  } else if (pll_.locked() && playing && session.have_timeline &&
             now_us - last_phase_us_ >= kPhaseGapUs) {
    // Start is the only *edge* downbeat; after that the PLL tracks
    // phase internally but Link only sees tempo writes — a standing
    // 0.5 % tempo error walks a beat off in ~200 beats, faster at
    // high BPM. Re-anchor on a MIDI quantum boundary when the
    // session has actually drifted (2 ms deadband so tick jitter
    // does not yank the grid every bar).
    const int64_t st = pll_.song_ticks();
    const uint32_t q =
        session.quantum_beats != 0 ? session.quantum_beats : 4u;
    if (st >= 0 && (st % 24) == 0 &&
        (st / 24) % static_cast<int64_t>(q) == 0 &&
        session.tempo_mbpm != 0) {
      const int64_t t_us = pll_.last_anchor_us();
      const double mpb =
          60000000000.0 / static_cast<double>(session.tempo_mbpm);
      const double beat_at =
          session.beat_at_origin +
          static_cast<double>(t_us - session.origin_us) / mpb;
      const double qf = static_cast<double>(q);
      double phase = beat_at - qf * std::floor(beat_at / qf);
      if (phase > qf * 0.5) {
        phase -= qf;
      }
      const double err_us = phase * mpb;
      if (err_us > static_cast<double>(kPhaseDeadbandUs) ||
          err_us < -static_cast<double>(kPhaseDeadbandUs)) {
        a.anchor_downbeat = true;
        a.downbeat_us = t_us;
        last_phase_us_ = now_us;
      }
    }
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
