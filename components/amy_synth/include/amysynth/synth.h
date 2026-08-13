#pragma once

// The synth seam.
//
// With CONFIG_NEON_AUDIO_AMY set, this is shorepine's AMY (MIT), vendored
// at third_party/amy. Without it, the same calls drive the small built-in
// SynthVoiceBank from neon_core. Either way the audio task, the MIDI
// router and the Synth role behave identically — which is what makes AMY
// optional rather than load-bearing.
//
// Every function here is called from the audio task except init(), which
// runs once before the task starts.

#include <cstdint>

namespace amysynth {

void init(uint32_t sample_rate, uint16_t block_frames);
void set_patch(uint8_t patch);

void note_on(uint8_t note, uint8_t velocity);
void note_off(uint8_t note);
void all_notes_off();

// Adds one block into out[frames] (mono, nominal ±1).
void render(float* out, uint32_t frames);

uint32_t active_voices();

// True when AMY is the engine behind these calls, for the status document.
bool using_amy();

}  // namespace amysynth
