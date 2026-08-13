#pragma once

#include <cstdint>

#include "neon/config/model.hpp"
#include "neon/timeline.hpp"

namespace neon {

// Session beat at t_us, Q32.32 signed.
int64_t beat_at_q32(const TimelineSnapshot& tl, int64_t t_us);

// Absolute time of the beat `beat_q32` (Q32.32 signed).
int64_t time_at_beat_q32(const TimelineSnapshot& tl, int64_t beat_q32);

// First loop (quantum) boundary strictly after t_us. Legacy boxes call
// this "the start of the next loop"; it is where quantized play/stop and
// the "reset on next loop" action land.
int64_t next_loop_boundary_us(const TimelineSnapshot& tl, int64_t t_us);

// --- Tap tempo -------------------------------------------------------
//
// Averages the intervals of a run of taps. A gap longer than kTimeoutUs
// starts a fresh run, and absurd intervals (outside the tempo range) are
// treated as the start of a new run rather than poisoning the average —
// which is what makes tapping recoverable in performance.
class TapTempo {
 public:
  static constexpr int kMaxIntervals = 7;
  static constexpr int64_t kTimeoutUs = 3000000;

  // Record a tap. Returns true and fills `milli_bpm` once at least two
  // taps of a run are in (the first tap of a run only sets the anchor).
  bool tap(int64_t t_us, uint32_t* milli_bpm);

  // Drop the run so the next tap starts over (menu exit, timeout sweep).
  void reset();

  int taps() const { return count_ + (have_last_ ? 1 : 0); }

 private:
  int64_t last_us_ = 0;
  bool have_last_ = false;
  int64_t intervals_[kMaxIntervals] = {};
  int count_ = 0;  // valid intervals
  int next_ = 0;   // ring cursor
};

// --- Tempo edits -----------------------------------------------------

uint32_t clamp_milli_bpm(int64_t milli_bpm);
// +/- whole BPM, as the legacy Up/Down buttons do.
uint32_t nudge_milli_bpm(uint32_t milli_bpm, int delta_bpm);
uint32_t double_milli_bpm(uint32_t milli_bpm);
uint32_t halve_milli_bpm(uint32_t milli_bpm);

// Double BPM → milli-BPM, rounded (32.89 → 32890, not 32889).
uint32_t milli_bpm_from_bpm(double bpm);
// Reconstruct milli-BPM from integer µs-per-beat, rounded so 33.0 BPM
// (1 818 182 µs) comes back as 33000, not 32999.
uint32_t milli_bpm_from_mpb_us(uint64_t mpb_us);

// --- Quantized transport --------------------------------------------
//
// Pressing play/stop takes effect at the start of the next loop so the
// module drops in on the downbeat. Arm with request(); the owner polls
// each service tick and commits when it fires.
class TransportLatch {
 public:
  // Arm a transition. Starting is immediate when the transport is already
  // stopped and there is no meaningful grid yet; stopping always waits for
  // the loop end. `quantized` false commits at now_us.
  void request(const TimelineSnapshot& tl, int64_t now_us, bool play,
               bool quantized = true);
  void cancel() { armed_ = false; }

  bool armed() const { return armed_; }
  bool pending_play() const { return play_; }
  int64_t fire_at_us() const { return fire_at_us_; }

  // Returns true once, when now_us reaches the armed boundary.
  bool poll(int64_t now_us, bool* play_out);

 private:
  bool armed_ = false;
  bool play_ = false;
  int64_t fire_at_us_ = 0;
};

// --- Resync ----------------------------------------------------------
//
// The legacy "Tap + Play" shift action, with both of its assignable
// behaviors:
//   kNextLoop  emit resets (and a MIDI Start) at the next loop boundary
//   kNow       re-align the Link grid so "now" becomes the downbeat
enum class ResyncMode : uint8_t {
  kNextLoop = 0,
  kNow = 1,
};

// The absolute time to hand to ILinkSession::request_beat_at_time().
int64_t resync_target_us(const TimelineSnapshot& tl, int64_t now_us,
                         ResyncMode mode);

}  // namespace neon
