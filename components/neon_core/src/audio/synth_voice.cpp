#include "neon/audio/synth_voice.hpp"

#include <cmath>

namespace neon {

namespace {

// patch → (waveform, attack ms, release ms, cutoff Hz)
struct Patch {
  uint8_t wave;  // 0 saw, 1 square, 2 sine, 3 saw+square
  float attack_ms;
  float release_ms;
  float cutoff_hz;
};

Patch patch_for(uint8_t p) {
  switch (p % SynthVoiceBank::kPatchCount) {
    case 1:
      return Patch{1, 1.0f, 120.0f, 2500.0f};
    case 2:
      return Patch{2, 8.0f, 400.0f, 6000.0f};
    case 3:
      return Patch{3, 0.5f, 60.0f, 1800.0f};
    default:
      return Patch{0, 2.0f, 250.0f, 3500.0f};
  }
}

float wave_sample(uint8_t wave, float phase) {
  switch (wave) {
    case 1:
      return phase < 0.5f ? 1.0f : -1.0f;
    case 2:
      return std::sin(phase * 6.28318530718f);
    case 3: {
      const float saw = 2.0f * phase - 1.0f;
      const float sq = phase < 0.5f ? 1.0f : -1.0f;
      return 0.6f * saw + 0.4f * sq;
    }
    default:
      return 2.0f * phase - 1.0f;
  }
}

}  // namespace

void SynthVoiceBank::reset(uint32_t sample_rate) {
  sample_rate_ = sample_rate != 0 ? sample_rate : 44100;
  for (auto& v : voices_) {
    v = Voice{};
  }
  counter_ = 0;
  for (int n = 0; n < 128; ++n) {
    freq_[n] = 440.0f * std::pow(2.0f, static_cast<float>(n - 69) / 12.0f);
  }
  set_patch(patch_);
}

void SynthVoiceBank::set_patch(uint8_t patch) {
  patch_ = patch;
  const Patch p = patch_for(patch);
  const float rate = static_cast<float>(sample_rate_);
  attack_ = 1.0f / (p.attack_ms * 0.001f * rate);
  release_ = std::exp(-1000.0f / (p.release_ms * rate));
  // One-pole lowpass coefficient for the patch's cutoff.
  const float x = std::exp(-6.28318530718f * p.cutoff_hz / rate);
  cutoff_ = 1.0f - x;
}

int SynthVoiceBank::allocate(uint8_t note) {
  // Same note retriggers its own voice; otherwise the oldest released
  // voice, otherwise the oldest voice outright.
  int free_idx = -1;
  int oldest = 0;
  for (int i = 0; i < kVoices; ++i) {
    if (voices_[i].gate && voices_[i].note == note) {
      return i;
    }
    if (!voices_[i].gate && voices_[i].env <= 0.0005f && free_idx < 0) {
      free_idx = i;
    }
    if (voices_[i].age < voices_[oldest].age) {
      oldest = i;
    }
  }
  return free_idx >= 0 ? free_idx : oldest;
}

void SynthVoiceBank::note_on(uint8_t note, uint8_t velocity) {
  if (note > 127) {
    return;
  }
  if (velocity == 0) {
    note_off(note);
    return;
  }
  const int i = allocate(note);
  Voice& v = voices_[i];
  v.note = note;
  v.gate = true;
  v.inc = freq_[note] / static_cast<float>(sample_rate_);
  // Scaled so every voice sounding at full velocity still lands inside
  // full scale: the mixer's saturator is a safety net, not a stage.
  v.amp = static_cast<float>(velocity) / 127.0f * (0.9f / kVoices);
  v.age = ++counter_;
  if (v.env <= 0.0005f) {
    v.phase = 0.0f;
    v.lp = 0.0f;
    v.env = 0.0f;
  }
}

void SynthVoiceBank::note_off(uint8_t note) {
  for (auto& v : voices_) {
    if (v.gate && v.note == note) {
      v.gate = false;
    }
  }
}

void SynthVoiceBank::all_notes_off() {
  for (auto& v : voices_) {
    v.gate = false;
  }
}

uint32_t SynthVoiceBank::active_voices() const {
  uint32_t n = 0;
  for (const auto& v : voices_) {
    if (v.gate || v.env > 0.0005f) {
      ++n;
    }
  }
  return n;
}

void SynthVoiceBank::render(float* out, uint32_t frames) {
  if (out == nullptr || frames == 0) {
    return;
  }
  const uint8_t wave = patch_for(patch_).wave;
  for (auto& v : voices_) {
    if (!v.gate && v.env <= 0.0005f) {
      continue;
    }
    for (uint32_t i = 0; i < frames; ++i) {
      if (v.gate) {
        v.env += attack_;
        if (v.env > 1.0f) {
          v.env = 1.0f;
        }
      } else {
        v.env *= release_;
        if (v.env < 0.0005f) {
          v.env = 0.0f;
        }
      }
      const float s = wave_sample(wave, v.phase);
      v.lp += cutoff_ * (s - v.lp);
      out[i] += v.lp * v.env * v.amp;
      v.phase += v.inc;
      if (v.phase >= 1.0f) {
        v.phase -= 1.0f;
      }
    }
  }
}

}  // namespace neon
