#pragma once

// A small polyphonic voice, driven by the same MIDI that already reaches
// the router (BLE or TRS), rendered on the Link timeline with the rest of
// the block.
//
// This is the fallback synth: when the AMY submodule is compiled in
// (CONFIG_NEON_AUDIO_AMY) the amy_synth component takes over the same
// role, and this stays as the stub leg's voice and the host tests'
// reference. Six voices of saw/square/sine through a one-pole filter and
// an AR envelope is not much of a synthesiser, but it is enough to hear
// that notes land where the beat does.

#include <cstdint>

namespace neon {

class SynthVoiceBank {
 public:
  static constexpr int kVoices = 6;
  static constexpr uint8_t kPatchCount = 4;

  void reset(uint32_t sample_rate);
  void set_patch(uint8_t patch);
  uint8_t patch() const { return patch_; }

  void note_on(uint8_t note, uint8_t velocity);
  void note_off(uint8_t note);
  void all_notes_off();

  // Adds into out[frames] (mono, nominal ±1).
  void render(float* out, uint32_t frames);

  uint32_t active_voices() const;

 private:
  struct Voice {
    uint8_t note = 0;
    bool gate = false;
    float phase = 0.0f;
    float inc = 0.0f;
    float env = 0.0f;
    float amp = 0.0f;
    float lp = 0.0f;
    uint32_t age = 0;
  };

  int allocate(uint8_t note);

  Voice voices_[kVoices] = {};
  uint32_t sample_rate_ = 44100;
  uint32_t counter_ = 0;
  uint8_t patch_ = 0;
  float attack_ = 0.0f;   // per-sample rise
  float release_ = 0.0f;  // per-sample decay factor
  float cutoff_ = 0.0f;   // one-pole coefficient
  float freq_[128] = {};
};

}  // namespace neon
