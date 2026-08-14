#pragma once

// The receive side of a Link Audio subscription.
//
// Blocks arrive from the network thread at the sender's rate and block
// size, with a beat window attached. The audio task pulls fixed-size
// blocks at its own rate off its own clock. Between the two sits this:
// a ring deep enough to absorb WiFi jitter, a resampler for the rate
// mismatch, and a servo that trims the resampler ±500 ppm so the buffer
// hovers at its fill target instead of slowly emptying or overflowing.
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

  // `storage` holds capacity_frames stereo int16 frames.
  void init(int16_t* storage, uint32_t capacity_frames, uint32_t out_rate);
  void configure(uint32_t jitter_ms);
  void reset();

  // Network side. Mono blocks are duplicated to stereo on the way in, so
  // everything downstream is one shape.
  void push(const AudioBlockInfo& info, const int16_t* interleaved);

  // Audio side. Always fills `frames` (silence while buffering or after an
  // underrun); returns the number of real frames produced.
  uint32_t pull(uint32_t frames, float* l, float* r);

  State state() const { return state_; }
  uint32_t fill_frames() const { return ring_.available() + stage_have_; }
  uint32_t target_frames() const { return target_frames_; }
  uint32_t underruns() const { return underruns_; }
  uint32_t concealed() const { return concealed_; }
  uint32_t dropped() const { return ring_.dropped(); }
  int32_t trim_ppm() const { return resampler_.trim_ppm(); }
  uint32_t sender_rate() const { return sender_rate_; }
  uint32_t jitter_ms() const { return jitter_ms_; }

  // Beat of the newest received frame, and of the frame the audio task is
  // about to play. Their difference is the buffered latency in beats.
  int64_t newest_beat_q32() const { return newest_beat_q32_; }
  int64_t read_beat_q32() const;

  static constexpr uint32_t kStageFrames = 512;
  static constexpr uint32_t kMinJitterMs = 5;
  static constexpr uint32_t kMaxJitterMs = 800;
  // A lost Live callback is a hole in beat time. We write up to this many
  // silent frames (with a short fade) instead of concatenating the two
  // sides of the hole — that concatenation is the crackle.
  static constexpr uint32_t kMaxConcealFrames = 960;  // 20 ms @ 48 k
  static constexpr uint32_t kFadeFrames = 64;         // ~1.3 ms @ 48 k

 private:
  void update_servo();
  void write_stereo(const int16_t* interleaved, uint32_t frames);
  void write_gap(uint32_t frames);
  void apply_out_fade(float* l, float* r, uint32_t frames);

  FrameRing ring_;
  LinearResampler resampler_;

  State state_ = State::kIdle;
  uint32_t out_rate_ = 44100;
  uint32_t sender_rate_ = 0;
  uint32_t jitter_ms_ = 60;
  uint32_t target_frames_ = 0;
  uint32_t underruns_ = 0;
  uint32_t concealed_ = 0;
  int32_t servo_ppm_ = 0;

  int64_t newest_beat_q32_ = 0;
  int64_t beat_per_frame_q32_ = 0;  // sender beats per sender frame, Q32.32
  bool have_prev_ = false;
  bool fade_in_next_ = false;
  int16_t last_l_ = 0;
  int16_t last_r_ = 0;

  // Output-side fade after a rebuffer (positive = fade in, used as count).
  uint32_t fade_in_left_ = 0;

  int16_t stage_[kStageFrames * 2] = {};
  uint32_t stage_have_ = 0;
};

}  // namespace neon
