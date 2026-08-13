#include "neon/audio/jitter_buffer.hpp"

#include <cstring>

#include "neon/fixed_math.hpp"

namespace neon {

namespace {

constexpr uint32_t kMaxPushFrames = 2048;

int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

void JitterBuffer::init(int16_t* storage, uint32_t capacity_frames,
                        uint32_t out_rate) {
  out_rate_ = out_rate != 0 ? out_rate : 44100;
  ring_.init(storage, capacity_frames, /*channels=*/2);
  resampler_.set_rates(out_rate_, out_rate_);
  configure(jitter_ms_);
  reset();
}

void JitterBuffer::configure(uint32_t jitter_ms) {
  if (jitter_ms < kMinJitterMs) jitter_ms = kMinJitterMs;
  if (jitter_ms > kMaxJitterMs) jitter_ms = kMaxJitterMs;
  jitter_ms_ = jitter_ms;
  const uint32_t rate = sender_rate_ != 0 ? sender_rate_ : out_rate_;
  uint32_t target = (rate / 1000u) * jitter_ms_;
  // Never target more than half the ring, or the servo has nowhere to go.
  const uint32_t cap = ring_.capacity() / 2;
  if (cap != 0 && target > cap) {
    target = cap;
  }
  target_frames_ = target;
}

void JitterBuffer::reset() {
  ring_.reset();
  resampler_.reset();
  resampler_.set_trim_ppm(0);
  servo_ppm_ = 0;
  stage_have_ = 0;
  underruns_ = 0;
  state_ = State::kIdle;
}

void JitterBuffer::push(const AudioBlockInfo& info, const int16_t* interleaved) {
  if (interleaved == nullptr || info.frames == 0 ||
      info.frames > kMaxPushFrames) {
    return;
  }
  if (info.sample_rate != 0 && info.sample_rate != sender_rate_) {
    sender_rate_ = info.sample_rate;
    resampler_.set_rates(sender_rate_, out_rate_);
    configure(jitter_ms_);
  }

  if (info.channels <= 1) {
    // Mono in, stereo everywhere after. Chunked so this stays off the
    // network thread's stack in any meaningful quantity.
    constexpr uint32_t kChunk = 128;
    int16_t stereo[kChunk * 2];
    uint32_t done = 0;
    while (done < info.frames) {
      const uint32_t n =
          info.frames - done < kChunk ? info.frames - done : kChunk;
      for (uint32_t i = 0; i < n; ++i) {
        stereo[2 * i] = interleaved[done + i];
        stereo[2 * i + 1] = interleaved[done + i];
      }
      ring_.write(stereo, n, /*drop_oldest=*/true);
      done += n;
    }
  } else {
    ring_.write(interleaved, info.frames, /*drop_oldest=*/true);
  }

  newest_beat_q32_ = info.end_beat_q32;
  const int64_t span = info.end_beat_q32 - info.begin_beat_q32;
  if (span > 0 && info.frames != 0) {
    beat_per_frame_q32_ = span / static_cast<int64_t>(info.frames);
  }
  if (state_ == State::kIdle) {
    state_ = State::kBuffering;
  }
}

int64_t JitterBuffer::read_beat_q32() const {
  // Everything still queued is audio the sender has already stamped, so
  // the frame about to be played sits exactly `fill` frames behind the
  // newest one received.
  const int64_t fill = static_cast<int64_t>(fill_frames());
  return newest_beat_q32_ - fill * beat_per_frame_q32_;
}

void JitterBuffer::update_servo() {
  if (target_frames_ == 0) {
    return;
  }
  const int32_t fill = static_cast<int32_t>(fill_frames());
  const int32_t err = fill - static_cast<int32_t>(target_frames_);
  // Full trim authority at one buffer-target of error, so a buffer that is
  // twice as full as it should be drains at 500 ppm.
  const int64_t raw = (static_cast<int64_t>(err) *
                       LinearResampler::kMaxTrimPpm) /
                      static_cast<int64_t>(target_frames_);
  const int32_t want =
      clamp_i32(raw > INT32_MAX ? INT32_MAX
                                : (raw < INT32_MIN ? INT32_MIN
                                                   : static_cast<int32_t>(raw)),
                -LinearResampler::kMaxTrimPpm, LinearResampler::kMaxTrimPpm);
  const int32_t delta = want - servo_ppm_;
  // One-eighth per block, with the last few ppm taken in one step so the
  // servo actually settles instead of asymptotically almost-settling.
  servo_ppm_ += (delta > -8 && delta < 8) ? delta : delta / 8;
  resampler_.set_trim_ppm(servo_ppm_);
}

uint32_t JitterBuffer::pull(uint32_t frames, float* l, float* r) {
  if (frames == 0) {
    return 0;
  }
  for (uint32_t i = 0; i < frames; ++i) {
    if (l != nullptr) l[i] = 0.0f;
    if (r != nullptr) r[i] = 0.0f;
  }
  if (state_ == State::kIdle) {
    return 0;
  }
  if (state_ == State::kBuffering) {
    if (fill_frames() < target_frames_) {
      return 0;
    }
    state_ = State::kPlaying;
    resampler_.reset();
  }

  update_servo();

  uint32_t produced = 0;
  while (produced < frames) {
    // Top the staging buffer up: the resampler needs one input frame past
    // the last one it interpolates from.
    if (stage_have_ < kStageFrames) {
      const uint32_t got = ring_.read(stage_ + stage_have_ * 2,
                                      kStageFrames - stage_have_);
      stage_have_ += got;
    }
    if (stage_have_ < 2) {
      break;
    }
    uint32_t consumed = 0;
    const uint32_t n = resampler_.process(
        stage_, stage_have_, &consumed, l != nullptr ? l + produced : nullptr,
        r != nullptr ? r + produced : nullptr, frames - produced);
    if (consumed != 0) {
      const uint32_t left = stage_have_ - consumed;
      if (left != 0) {
        std::memmove(stage_, stage_ + consumed * 2,
                     static_cast<size_t>(left) * 2 * sizeof(int16_t));
      }
      stage_have_ = left;
    }
    produced += n;
    if (n == 0 && consumed == 0) {
      break;
    }
  }

  if (produced < frames) {
    ++underruns_;
    state_ = State::kBuffering;
  }
  return produced;
}

}  // namespace neon
