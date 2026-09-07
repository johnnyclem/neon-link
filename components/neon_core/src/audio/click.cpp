#include "neon/audio/click.hpp"

#include <cmath>

namespace neon {

namespace {

constexpr float kTwoPi = 6.28318530718f;

struct Voice {
  float f1;
  float f2;
  float mix2;
  float decay_ms;
  uint32_t len_ms;
  bool noise;
  float amp;
};

// Accent is the same voice, louder and a touch longer, always on beat 1.
Voice voice_for(ClickSound sound, bool accent) {
  switch (sound) {
    case ClickSound::kNoise:
      // Pitchless tick: short noise burst, quiet off-beats.
      return Voice{0.0f, 0.0f, 0.0f, accent ? 5.0f : 3.0f,
                   accent ? 16u : 9u, true, accent ? 1.0f : 0.42f};
    case ClickSound::kWood:
      return Voice{accent ? 1320.0f : 880.0f, accent ? 1980.0f : 1760.0f,
                   0.35f, accent ? 8.0f : 6.0f, accent ? 28u : 20u, false,
                   accent ? 1.0f : 0.7f};
    case ClickSound::kSine:
    default:
      return Voice{accent ? 1500.0f : 1000.0f, 0.0f, 0.0f,
                   accent ? 28.0f : 22.0f, 90u, false, accent ? 1.0f : 0.72f};
  }
}

}  // namespace

void ClickSynth::reset(uint32_t sample_rate) {
  sample_rate_ = sample_rate != 0 ? sample_rate : 44100;
  remaining_ = 0;
  fade_remaining_ = 0;
  fade_len_ = 0;
  env_ = 0.0f;
  phase_ = 0.0f;
  phase2_ = 0.0f;
  last_onsets_ = 0;
  last_onset_frame_ = 0;
  last_onset_accent_ = false;
}

void ClickSynth::fade_out() {
  if (remaining_ == 0) {
    return;
  }
  const uint32_t len = (sample_rate_ * kFadeMs) / 1000u;
  fade_len_ = len != 0 ? len : 1;
  if (fade_remaining_ == 0 || fade_remaining_ > fade_len_) {
    fade_remaining_ = fade_len_;
  }
  if (remaining_ > fade_len_) {
    remaining_ = fade_len_;
  }
}

void ClickSynth::trigger(bool accent) {
  const Voice v = voice_for(cfg_.sound, accent);
  const float rate = static_cast<float>(sample_rate_);
  remaining_ = (sample_rate_ * v.len_ms) / 1000u;
  if (remaining_ == 0) {
    remaining_ = 1;
  }
  fade_remaining_ = 0;
  fade_len_ = 0;
  env_ = 1.0f;
  // Per-sample factor for an exponential decay with the given time
  // constant: exp(-1 / (tau * rate)).
  env_decay_ = std::exp(-1000.0f / (v.decay_ms * rate));
  phase_ = 0.0f;
  phase2_ = 0.0f;
  phase_inc_ = kTwoPi * v.f1 / rate;
  phase2_inc_ = kTwoPi * v.f2 / rate;
  mix2_ = v.mix2;
  noise_ = v.noise;
  amp_ = v.amp * gain_from_byte(cfg_.gain);
  // Fixed seed per hit: two identical clicks must render identically.
  rng_ = 0x9e3779b9u;
}

float ClickSynth::next_sample() {
  if (remaining_ == 0) {
    return 0.0f;
  }
  float s;
  if (noise_) {
    rng_ = rng_ * 1664525u + 1013904223u;
    s = static_cast<float>(static_cast<int32_t>(rng_ >> 8)) *
        (1.0f / 8388608.0f);
    s -= 1.0f;
  } else {
    s = std::sin(phase_);
    if (mix2_ != 0.0f) {
      s = (1.0f - mix2_) * s + mix2_ * std::sin(phase2_);
    }
    phase_ += phase_inc_;
    if (phase_ > kTwoPi) {
      phase_ -= kTwoPi;
    }
    phase2_ += phase2_inc_;
    if (phase2_ > kTwoPi) {
      phase2_ -= kTwoPi;
    }
  }
  float out = s * env_ * amp_;
  if (fade_remaining_ != 0 && fade_len_ != 0) {
    out *= static_cast<float>(fade_remaining_) / static_cast<float>(fade_len_);
    --fade_remaining_;
    if (fade_remaining_ == 0) {
      remaining_ = 0;
      return out;
    }
  }
  env_ *= env_decay_;
  --remaining_;
  return out;
}

void ClickSynth::render(const BeatWindow& w, float* out, uint32_t frames) {
  last_onsets_ = 0;
  if (out == nullptr || frames == 0) {
    return;
  }

  // Onset frames for every whole beat inside [begin, end). Blocks are
  // contiguous and the interval is half-open, so no beat fires twice.
  constexpr uint32_t kMaxOnsets = 16;
  uint32_t onset_frame[kMaxOnsets];
  bool onset_accent[kMaxOnsets];
  uint32_t onsets = 0;

  const bool sounding_allowed =
      cfg_.enabled && (w.playing || !cfg_.follow_transport);
  if (sounding_allowed && w.valid) {
    int64_t k = floor_beat(w.begin_q32);
    if (beat_q32_from_int(k) < w.begin_q32) {
      ++k;
    }
    while (beat_q32_from_int(k) < w.end_q32 && onsets < kMaxOnsets) {
      onset_frame[onsets] = w.frame_of_beat(beat_q32_from_int(k));
      onset_accent[onsets] =
          cfg_.accent && beat_in_bar(k, w.quantum_beats) == 0;
      ++onsets;
      ++k;
    }
  }

  uint32_t next = 0;
  for (uint32_t i = 0; i < frames; ++i) {
    while (next < onsets && onset_frame[next] == i) {
      trigger(onset_accent[next]);
      last_onset_frame_ = i;
      last_onset_accent_ = onset_accent[next];
      ++last_onsets_;
      ++next;
    }
    out[i] += next_sample();
  }
}

}  // namespace neon
