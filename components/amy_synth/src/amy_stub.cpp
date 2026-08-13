// The synth seam without AMY: neon_core's own SynthVoiceBank.
//
// This is what the stub CI leg and any build with CONFIG_NEON_AUDIO_AMY
// off get. It is a real voice, not a silence generator — the Synth role,
// the MIDI routing and the gain control are all exercised by it, so
// turning AMY on later changes the timbre and nothing else.

#include "amysynth/synth.h"

#include "neon/audio/synth_voice.hpp"

namespace amysynth {

namespace {
neon::SynthVoiceBank g_bank;
}

void init(uint32_t sample_rate, uint16_t /*block_frames*/) {
  g_bank.reset(sample_rate);
}

void set_patch(uint8_t patch) { g_bank.set_patch(patch); }

void note_on(uint8_t note, uint8_t velocity) { g_bank.note_on(note, velocity); }

void note_off(uint8_t note) { g_bank.note_off(note); }

void all_notes_off() { g_bank.all_notes_off(); }

void render(float* out, uint32_t frames) { g_bank.render(out, frames); }

uint32_t active_voices() { return g_bank.active_voices(); }

bool using_amy() { return false; }

}  // namespace amysynth
