#pragma once

#include <cstdint>

// The audio engine on the Seed's built-in codec: the portable metronome
// click, pulse-as-audio taps (clock / reset / run roles), and mixer,
// rendered inside libDaisy's audio callback (SAI DMA interrupt) against
// the session grid via the portable SampleClock. The codec runs 48 kHz —
// exactly the rate the ESP audio service is written for.
//
// Single-core seqlock rule: the callback runs in interrupt context, so
// it must NOT read the seqlock buses — a mid-publish writer on the main
// loop could never finish under a spinning ISR. poll() stages the
// timeline, engine config, and audio config into plain structs under an
// IRQ mask; the callback reads the staging copies, which are stable for
// the whole block.
namespace audioeng {

// Starts the codec + callback. Call after DaisySeed::Init() and
// neon_config_load().
void init();

// Stage bus state for the callback and publish meters. Call every loop.
void poll(int64_t now_us);

}  // namespace audioeng
