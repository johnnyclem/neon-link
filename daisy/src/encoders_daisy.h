#pragma once

#include <cstdint>

// The board's EC11 encoders (kNumEncoders of them) + push switches,
// decoded by the portable
// neon::QuadDecoder from the 10 kHz input sampler (the pulse-timer ISR)
// rather than pin-change interrupts — libDaisy has no EXTI wrapper, and
// 10 kHz oversampling comfortably beats any human detent rate. libDaisy
// also ships an Encoder class, but the portable decoder is host-tested;
// prefer it (HANDOFF: same call as the Teensy build).
namespace enc {

enum class ButtonEvent : uint8_t { kNone, kClick, kLongPress };

void init();

// Called from the 10 kHz sampling ISR.
void sample_isr();

// Accumulated detents since the last call (signed).
int take_detents(int index);

// Debounced click / 600 ms long-press state machine, polled per loop.
ButtonEvent poll_button(int index, uint32_t now_ms);

}  // namespace enc
