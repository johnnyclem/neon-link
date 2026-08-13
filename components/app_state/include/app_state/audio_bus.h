#pragma once

#include <cstdint>

#include "neon/audio/types.hpp"
#include "neon/timeline.hpp"

// The buses between core 0 and the audio task, alongside the existing
// timeline / engine-config ones. Same shape: seqlocks for state, a small
// FreeRTOS queue for events.

// Live-applied audio configuration: written whenever the config changes,
// read by the audio task once per block.
neon::SeqLock<neon::AudioEngineConfig>& audio_config_bus();

// Meters, counters and subscription state, published by the audio task
// for /api/status and the panel.
neon::SeqLock<neon::AudioStatus>& audio_status_bus();

// MIDI notes for the synth voice: the core-0 router pushes, the audio task
// pops at the top of each block. A dropped note-on is better than a stalled
// render, so the queue never blocks.
struct SynthEvent {
  uint8_t note;
  uint8_t velocity;
  uint8_t on;       // 0 = note off
  uint8_t all_off;  // 1 = panic, ignore note/velocity
};
bool synth_queue_push(const SynthEvent& ev);
bool synth_queue_pop(SynthEvent* ev);
