#pragma once

// The metronome. One voice, retriggered on every whole beat inside the
// block's beat window, with an accent on the downbeat.
//
// Scheduling is stateless per block: the onset frame is recomputed from
// the window every time, so a fresh timeline snapshot re-anchors the click
// at the next block (≤3 ms) instead of dragging a running counter behind
// it. Only the sounding envelope carries state, and it is allowed to
// finish — a click cut mid-envelope is a pop.

#include <cstdint>

#include "neon/audio/beat_window.hpp"
#include "neon/audio/types.hpp"

namespace neon {

struct ClickConfig {
  bool enabled = false;
  ClickSound sound = ClickSound::kSine;
  uint8_t gain = kUnityGainByte;
  bool accent = true;
  // Metronomes follow the transport; turning this off gives a click that
  // keeps ticking on the free-running beat grid while stopped.
  bool follow_transport = true;
};

class ClickSynth {
 public:
  void reset(uint32_t sample_rate);
  void set_config(const ClickConfig& cfg) { cfg_ = cfg; }
  const ClickConfig& config() const { return cfg_; }

  // Adds the click into out[frames] (mono, nominal ±1). `out` is not
  // cleared: the caller owns the buffer's contents.
  void render(const BeatWindow& w, float* out, uint32_t frames);

  // Transport stop or a phase jump: fade whatever is sounding to silence
  // inside kFadeMs instead of leaving a DC step behind.
  void fade_out();

  bool voice_active() const { return remaining_ != 0; }

  // Number of onsets the last render() placed — the host tests assert on
  // this rather than fishing for the first non-zero sample.
  uint32_t last_onsets() const { return last_onsets_; }
  uint32_t last_onset_frame() const { return last_onset_frame_; }
  bool last_onset_accent() const { return last_onset_accent_; }

  static constexpr uint32_t kFadeMs = 1;

 private:
  void trigger(bool accent);
  float next_sample();

  ClickConfig cfg_{};
  uint32_t sample_rate_ = 44100;

  // Voice state.
  uint32_t remaining_ = 0;
  uint32_t fade_remaining_ = 0;
  uint32_t fade_len_ = 0;
  float env_ = 0.0f;
  float env_decay_ = 0.0f;
  float phase_ = 0.0f;
  float phase_inc_ = 0.0f;
  float phase2_ = 0.0f;
  float phase2_inc_ = 0.0f;
  float mix2_ = 0.0f;
  float amp_ = 0.0f;
  bool noise_ = false;
  uint32_t rng_ = 0;

  uint32_t last_onsets_ = 0;
  uint32_t last_onset_frame_ = 0;
  bool last_onset_accent_ = false;
};

}  // namespace neon
