#include "neon/audio/jitter_buffer.hpp"

#include <cstring>

#include "neon/fixed_math.hpp"

namespace neon {

namespace {

constexpr uint32_t kMaxPushFrames = 2048;

int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Round-to-nearest signed division, denominator > 0.
int64_t div_round(int64_t num, int64_t den) {
  return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
}

}  // namespace

void JitterBuffer::init(int16_t* storage, uint32_t capacity_frames,
                        uint32_t out_rate) {
  out_rate_ = out_rate != 0 ? out_rate : 44100;
  buf_ = storage;
  capacity_ = floor_pow2(capacity_frames);
  mask_ = capacity_ != 0 ? capacity_ - 1 : 0;
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
  const uint32_t cap = capacity_ / 2;
  if (cap != 0 && target > cap) {
    target = cap;
  }
  target_frames_ = target;
}

void JitterBuffer::reset() {
  rd_ = 0;
  wr_ = 0;
  dropped_ = 0;
  resampler_.reset();
  resampler_.set_trim_ppm(0);
  servo_ppm_ = 0;
  stage_have_ = 0;
  underruns_ = 0;
  concealed_ = 0;
  have_prev_ = false;
  fade_in_left_ = 0;
  last_l_ = 0;
  last_r_ = 0;
  newest_beat_q32_ = 0;
  beat_per_frame_q32_ = 0;
  state_ = State::kIdle;
}

// Drop the oldest frames so the span [rd_, end) fits the ring.
void JitterBuffer::make_room(uint64_t end) {
  if (end <= rd_ + capacity_) {
    return;
  }
  const uint64_t need = end - (rd_ + capacity_);
  rd_ += need;
  dropped_ += static_cast<uint32_t>(need);
  if (rd_ > wr_) {
    wr_ = rd_;
  }
}

// Raw positional copy of stereo frames into the ring, wrap included. The
// caller has already made room; nothing here moves a cursor.
void JitterBuffer::copy_in(uint64_t pos, const int16_t* stereo,
                           uint32_t frames) {
  const uint32_t at = static_cast<uint32_t>(pos) & mask_;
  const uint32_t first = frames < capacity_ - at ? frames : capacity_ - at;
  std::memcpy(buf_ + static_cast<size_t>(at) * 2, stereo,
              static_cast<size_t>(first) * 2 * sizeof(int16_t));
  if (frames > first) {
    std::memcpy(buf_, stereo + static_cast<size_t>(first) * 2,
                static_cast<size_t>(frames - first) * 2 * sizeof(int16_t));
  }
}

uint32_t JitterBuffer::ring_read(int16_t* out, uint32_t frames) {
  const uint32_t have = static_cast<uint32_t>(wr_ - rd_);
  const uint32_t n = frames < have ? frames : have;
  if (n == 0) {
    return 0;
  }
  const uint32_t at = static_cast<uint32_t>(rd_) & mask_;
  const uint32_t first = n < capacity_ - at ? n : capacity_ - at;
  std::memcpy(out, buf_ + static_cast<size_t>(at) * 2,
              static_cast<size_t>(first) * 2 * sizeof(int16_t));
  if (n > first) {
    std::memcpy(out + static_cast<size_t>(first) * 2, buf_,
                static_cast<size_t>(n - first) * 2 * sizeof(int16_t));
  }
  rd_ += n;
  return n;
}

// Claim [wr_, wr_ + frames) as silence: the tail sample fades out into it
// so a hole is a dip, not a click. A late packet that fills the hole later
// simply overwrites this.
void JitterBuffer::write_gap(uint32_t frames) {
  constexpr uint32_t kChunk = 128;
  int16_t buf[kChunk * 2];
  make_room(wr_ + frames);
  uint32_t done = 0;
  const uint32_t fade = frames < kFadeFrames ? frames : kFadeFrames;
  while (done < frames) {
    const uint32_t n = frames - done < kChunk ? frames - done : kChunk;
    for (uint32_t i = 0; i < n; ++i) {
      const uint32_t idx = done + i;
      if (idx < fade) {
        const float g = 1.0f - static_cast<float>(idx) / static_cast<float>(fade);
        buf[2 * i] = static_cast<int16_t>(static_cast<float>(last_l_) * g);
        buf[2 * i + 1] = static_cast<int16_t>(static_cast<float>(last_r_) * g);
      } else {
        buf[2 * i] = 0;
        buf[2 * i + 1] = 0;
      }
    }
    copy_in(wr_ + done, buf, n);
    done += n;
  }
  wr_ += frames;
  last_l_ = 0;
  last_r_ = 0;
}

// Convert (mono duplicated to stereo, optional fade-in) and write the
// block's frames [skip, info.frames) at ring position `pos`.
void JitterBuffer::place_block(uint64_t pos, const AudioBlockInfo& info,
                               const int16_t* interleaved, uint32_t skip,
                               bool fade_in) {
  constexpr uint32_t kChunk = 128;
  int16_t stereo[kChunk * 2];
  const uint32_t fade_n =
      fade_in ? (info.frames < kFadeFrames ? info.frames : kFadeFrames) : 0;
  uint32_t done = skip;
  uint64_t at = pos;
  while (done < info.frames) {
    const uint32_t n =
        info.frames - done < kChunk ? info.frames - done : kChunk;
    for (uint32_t i = 0; i < n; ++i) {
      int16_t lv;
      int16_t rv;
      if (info.channels <= 1) {
        lv = rv = interleaved[done + i];
      } else {
        lv = interleaved[(done + i) * 2];
        rv = interleaved[(done + i) * 2 + 1];
      }
      if (done + i < fade_n) {
        const float g =
            static_cast<float>(done + i) / static_cast<float>(fade_n);
        lv = static_cast<int16_t>(static_cast<float>(lv) * g);
        rv = static_cast<int16_t>(static_cast<float>(rv) * g);
      }
      stereo[2 * i] = lv;
      stereo[2 * i + 1] = rv;
    }
    copy_in(at, stereo, n);
    at += n;
    done += n;
  }
}

void JitterBuffer::push(const AudioBlockInfo& info, const int16_t* interleaved) {
  if (buf_ == nullptr || interleaved == nullptr || info.frames == 0 ||
      info.frames > kMaxPushFrames || info.frames > capacity_) {
    return;
  }
  if (info.sample_rate != 0 && info.sample_rate != sender_rate_) {
    sender_rate_ = info.sample_rate;
    resampler_.set_rates(sender_rate_, out_rate_);
    configure(jitter_ms_);
  }

  // Where does this block belong? Default: append at the high-water mark.
  bool fade_in = false;
  int64_t pos = static_cast<int64_t>(wr_);

  if (have_prev_ && beat_per_frame_q32_ > 0 && info.begin_beat_q32 != 0) {
    const int64_t bpf = beat_per_frame_q32_;
    const int64_t delta_beats = info.begin_beat_q32 - newest_beat_q32_;
    // Half a frame of slack either way: stamp jitter, not a hole.
    if (delta_beats > bpf + bpf / 2) {
      // Forward hole: silence at its timeline position, up to the cap.
      // Past the cap it is a forward playhead jump — insert the capped
      // gap and let the append below re-anchor the beat map.
      int64_t gap = delta_beats / bpf;
      if (gap > static_cast<int64_t>(kMaxConcealFrames)) {
        gap = static_cast<int64_t>(kMaxConcealFrames);
      }
      if (gap > static_cast<int64_t>(capacity_ / 2)) {
        gap = static_cast<int64_t>(capacity_ / 2);
      }
      write_gap(static_cast<uint32_t>(gap));
      ++concealed_;
      fade_in = true;
      pos = static_cast<int64_t>(wr_);
    } else if (delta_beats < -(bpf + bpf / 2)) {
      const int64_t back = div_round(-delta_beats, bpf);
      if (back <= static_cast<int64_t>(kMaxReorderFrames)) {
        // A late block: reordered, or a retransmitted duplicate. Place it
        // at the position its beats name — into the silent hole it was
        // concealed with, or harmlessly over its own earlier copy.
        pos = static_cast<int64_t>(wr_) - back;
        if (pos + static_cast<int64_t>(info.frames) <=
            static_cast<int64_t>(rd_)) {
          dropped_ += info.frames;  // its playback deadline already passed
          return;
        }
      }
      // else: a backward timeline jump (loop wrap, relocated playhead).
      // The sender's audio is continuous across it, so append at wr_ and
      // let the beat anchor re-establish below.
    }
  }

  const uint64_t end = static_cast<uint64_t>(pos + info.frames);
  make_room(end);
  uint32_t skip = 0;
  if (pos < static_cast<int64_t>(rd_)) {
    // The head of a late block has already been played out; clip it.
    skip = static_cast<uint32_t>(static_cast<int64_t>(rd_) - pos);
    pos = static_cast<int64_t>(rd_);
    dropped_ += skip;
    if (skip >= info.frames) {
      return;
    }
  }

  place_block(static_cast<uint64_t>(pos), info, interleaved, skip, fade_in);

  if (end > wr_) {
    wr_ = end;
    last_l_ = buf_[(static_cast<uint32_t>(end - 1) & mask_) * 2];
    last_r_ = buf_[(static_cast<uint32_t>(end - 1) & mask_) * 2 + 1];
    newest_beat_q32_ = info.end_beat_q32;
    const int64_t span = info.end_beat_q32 - info.begin_beat_q32;
    if (span > 0 && info.frames != 0) {
      beat_per_frame_q32_ = span / static_cast<int64_t>(info.frames);
    }
    have_prev_ = true;
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
    fade_in_left_ = kFadeFrames;
  }

  update_servo();

  uint32_t produced = 0;
  while (produced < frames) {
    // Top the staging buffer up: the resampler needs one input frame past
    // the last one it interpolates from.
    if (stage_have_ < kStageFrames) {
      const uint32_t got = ring_read(stage_ + stage_have_ * 2,
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
    // Fade the last good samples into the silence already written past
    // `produced`, so an underrun is a duck instead of a click.
    const uint32_t fade = produced < kFadeFrames ? produced : kFadeFrames;
    if (fade != 0) {
      const uint32_t start = produced - fade;
      for (uint32_t i = 0; i < fade; ++i) {
        const float g = 1.0f - static_cast<float>(i) / static_cast<float>(fade);
        if (l != nullptr) l[start + i] *= g;
        if (r != nullptr) r[start + i] *= g;
      }
    }
  } else {
    apply_out_fade(l, r, frames);
  }
  return produced;
}

void JitterBuffer::apply_out_fade(float* l, float* r, uint32_t frames) {
  if (fade_in_left_ == 0 || frames == 0) {
    return;
  }
  uint32_t i = 0;
  while (i < frames && fade_in_left_ > 0) {
    const float g = 1.0f - static_cast<float>(fade_in_left_) /
                               static_cast<float>(kFadeFrames);
    if (l != nullptr) l[i] *= g;
    if (r != nullptr) r[i] *= g;
    --fade_in_left_;
    ++i;
  }
}

}  // namespace neon
