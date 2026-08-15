#pragma once

// The receive side of a Link Audio subscription.
//
// Blocks arrive from the network at the sender's rate and block size,
// with a beat window attached. The audio task pulls fixed-size blocks at
// its own rate off its own clock. Between the two sits this: a
// beat-addressed ring deep enough to absorb WiFi jitter, a resampler for
// the rate mismatch, and a servo that trims the resampler ±500 ppm so the
// buffer hovers at its fill target instead of slowly emptying or
// overflowing.
//
// Writes are placed, not appended: each block lands at the ring position
// its begin beat implies. A lost packet leaves a silent hole at its exact
// timeline position, a reordered packet drops into the hole it belongs
// in, and a duplicate is an idempotent overwrite — arrival order stopped
// mattering, only beat order does. A block whose beats are far away is a
// timeline jump (a loop wrap, a relocated playhead): the audio around a
// jump is continuous at the sender, so it is appended and the beat anchor
// re-established rather than torn apart.
//
// Placement makes the write side stateful in a way an SPSC ring is not,
// so push() and pull() must run on the same thread. That is how
// audio_service wires it: the audio task itself drains the network block
// ring (the actual SPSC seam) and pushes here.
//
// The fill target *is* the latency: `jitter_ms` of audio is always held
// back, and the beat of the frame about to be played is therefore the
// newest received beat minus that much. read_beat_q32() reports it, which
// is what makes the alignment checkable rather than a matter of taste.

#include <cstdint>

#include "neon/audio/frame_ring.hpp"
#include "neon/audio/resampler.hpp"

namespace neon {

class JitterBuffer {
 public:
  enum class State : uint8_t { kIdle = 0, kBuffering = 1, kPlaying = 2 };

  // `storage` holds capacity_frames stereo int16 frames (rounded down to
  // a power of two).
  void init(int16_t* storage, uint32_t capacity_frames, uint32_t out_rate);
  void configure(uint32_t jitter_ms);
  void reset();

  // Write side. Mono blocks are duplicated to stereo on the way in, so
  // everything downstream is one shape.
  void push(const AudioBlockInfo& info, const int16_t* interleaved);

  // Audio side. Always fills `frames` (silence while buffering or after an
  // underrun); returns the number of real frames produced.
  uint32_t pull(uint32_t frames, float* l, float* r);

  State state() const { return state_; }
  uint32_t fill_frames() const {
    return static_cast<uint32_t>(wr_ - rd_) + stage_have_;
  }
  uint32_t target_frames() const { return target_frames_; }
  uint32_t underruns() const { return underruns_; }
  uint32_t concealed() const { return concealed_; }
  uint32_t dropped() const { return dropped_; }
  int32_t trim_ppm() const { return resampler_.trim_ppm(); }
  uint32_t sender_rate() const { return sender_rate_; }
  // Requested target, after the kMinJitterMs..kMaxJitterMs sanity clamp
  // but before configure()'s further clamp to half the ring's capacity.
  uint32_t jitter_ms() const { return jitter_ms_; }
  // What target_frames() actually comes out to in milliseconds — differs
  // from jitter_ms() exactly when the ring is too small to honor the
  // request (docs/STUDIO_MODE_TEST_PLAN.md P3/§C5: that clamp used to be
  // silent).
  uint32_t effective_jitter_ms() const { return effective_jitter_ms_; }

  // Beat of the newest received frame, and of the frame the audio task is
  // about to play. Their difference is the buffered latency in beats.
  int64_t newest_beat_q32() const { return newest_beat_q32_; }
  int64_t read_beat_q32() const;

  static constexpr uint32_t kStageFrames = 512;
  static constexpr uint32_t kMinJitterMs = 5;
  static constexpr uint32_t kMaxJitterMs = 800;
  // A hole in beat time — lost packets, or a lost sender callback — is
  // written as silence (with a short fade) at its timeline position, up
  // to this much. A larger gap re-anchors instead: it is a playhead jump,
  // not a loss worth a longer wait.
  static constexpr uint32_t kMaxConcealFrames = 4800;  // 100 ms @ 48 k
  // How far behind the newest beat a block may land and still be placed.
  // UDP reordering displaces packets by a few packet times; anything
  // further back is a backward timeline jump (a loop wrap) where the
  // sender's audio is continuous, so it appends instead.
  static constexpr uint32_t kMaxReorderFrames = 2048;  // ~43 ms @ 48 k
  static constexpr uint32_t kFadeFrames = 64;          // ~1.3 ms @ 48 k

 private:
  void update_servo();
  void make_room(uint64_t end);
  void copy_in(uint64_t pos, const int16_t* stereo, uint32_t frames);
  void place_block(uint64_t pos, const AudioBlockInfo& info,
                   const int16_t* interleaved, uint32_t skip, bool fade_in);
  uint32_t ring_read(int16_t* out, uint32_t frames);
  void write_gap(uint32_t frames);
  void apply_out_fade(float* l, float* r, uint32_t frames);

  LinearResampler resampler_;

  int16_t* buf_ = nullptr;
  uint32_t capacity_ = 0;
  uint32_t mask_ = 0;
  // Absolute frame cursors: rd_ is the next frame the audio side will
  // consume, wr_ the high-water mark (one past the newest frame placed).
  uint64_t rd_ = 0;
  uint64_t wr_ = 0;

  State state_ = State::kIdle;
  uint32_t out_rate_ = 44100;
  uint32_t sender_rate_ = 0;
  uint32_t jitter_ms_ = 60;
  uint32_t target_frames_ = 0;
  uint32_t effective_jitter_ms_ = 0;
  uint32_t underruns_ = 0;
  uint32_t concealed_ = 0;
  uint32_t dropped_ = 0;
  int32_t servo_ppm_ = 0;

  int64_t newest_beat_q32_ = 0;     // beat of the frame at wr_
  int64_t beat_per_frame_q32_ = 0;  // sender beats per sender frame, Q32.32
  bool have_prev_ = false;
  int16_t last_l_ = 0;
  int16_t last_r_ = 0;

  // Output-side fade after a rebuffer (positive = fade in, used as count).
  uint32_t fade_in_left_ = 0;

  int16_t stage_[kStageFrames * 2] = {};
  uint32_t stage_have_ = 0;
};

}  // namespace neon
