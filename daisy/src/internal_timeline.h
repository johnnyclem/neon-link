#pragma once

#include <cstdint>

#include "neon/timeline.hpp"
#include "neon/transport.hpp"

// The session timeline for the Daisy Seed: no network interface on this
// hardware means no Ableton Link session, so the module runs its own
// grid (docs/DAISY.md §1). Recovered from the Teensy target's v1
// (pre-Link) build — `git show 3934833:teensy41/src/internal_timeline.h`.
//
// Produces the same TimelineSnapshot Link produces on the other targets,
// so the pulse engine, the UI phase bar, MIDI clock, audio beat windows,
// and the ext-clock follower are driven identically — every consumer
// reads the snapshot and cannot tell the difference. If a networked
// variant ever lands (USB gadget networking, docs/DAISY.md §6), only the
// writer of this snapshot changes.
class InternalTimeline {
 public:
  void init(uint32_t milli_bpm, uint32_t quantum_beats, int64_t now_us) {
    tl_.tempo_mpb_q32 = mpb_q32_from_milli_bpm(milli_bpm);
    tl_.origin_us = now_us;
    tl_.beat_at_origin_q32 = 0;
    tl_.quantum_beats = quantum_beats != 0 ? quantum_beats : 4;
    tl_.playing = 0;
    tl_.num_peers = 0;
    milli_bpm_ = milli_bpm;
    ++version_;
  }

  // Re-anchors at now so the beat grid stays continuous across the edit.
  void set_tempo(uint32_t milli_bpm, int64_t now_us) {
    tl_.beat_at_origin_q32 = neon::beat_at_q32(tl_, now_us);
    tl_.origin_us = now_us;
    tl_.tempo_mpb_q32 = mpb_q32_from_milli_bpm(milli_bpm);
    milli_bpm_ = milli_bpm;
    ++version_;
  }

  void set_quantum(uint32_t quantum_beats, int64_t now_us) {
    tl_.beat_at_origin_q32 = neon::beat_at_q32(tl_, now_us);
    tl_.origin_us = now_us;
    tl_.quantum_beats = quantum_beats != 0 ? quantum_beats : 4;
    ++version_;
  }

  // Play restarts the grid with beat 0 (the downbeat) at now, which is
  // what makes the start-of-play Reset pulse land where sequencers
  // expect it. Stop freezes the phase.
  void set_playing(bool play, int64_t now_us) {
    if (play == (tl_.playing != 0)) {
      return;
    }
    if (play) {
      tl_.beat_at_origin_q32 = 0;
    } else {
      tl_.beat_at_origin_q32 = neon::beat_at_q32(tl_, now_us);
    }
    tl_.origin_us = now_us;
    tl_.playing = play ? 1 : 0;
    ++version_;
  }

  // Place the downbeat (beat 0 mod quantum) at t_us — what
  // ILinkSession::request_beat_at_time does to a Link session. Serves
  // both resync modes and the RST IN phase anchor; t_us may be in the
  // future (the next loop boundary), which just makes the current beat
  // count negative until it arrives — the phase math handles that.
  void anchor_downbeat(int64_t t_us) {
    tl_.beat_at_origin_q32 = 0;
    tl_.origin_us = t_us;
    ++version_;
  }

  bool playing() const { return tl_.playing != 0; }
  uint32_t milli_bpm() const { return milli_bpm_; }
  const neon::TimelineSnapshot& snapshot() const { return tl_; }
  uint32_t version() const { return version_; }

 private:
  static uint64_t mpb_q32_from_milli_bpm(uint32_t milli_bpm) {
    // µs per beat = 60e6 / BPM = 6e10 / milli-BPM, in Q32.32.
    return static_cast<uint64_t>(
        (60000000000.0 / static_cast<double>(milli_bpm)) * 4294967296.0);
  }

  neon::TimelineSnapshot tl_{};
  uint32_t milli_bpm_ = 120000;
  uint32_t version_ = 0;
};
