#pragma once

// Board dispatch for the Daisy-family targets. Every source includes
// this header; the per-board Makefile's NEON_BOARD_* define picks the
// real pin map. Each board header is the single source of truth for its
// wiring — docs/DAISY.md mirrors them as tables.
//
// Every board header provides the same vocabulary:
//   kNumEncoders / kEncPins[][3]      EC11s (A, B, switch); may be 0
//   kNumButtons  / kButtonPins[]      debounced momentary switches
//   kNumPulsePins / kPulsePins[] /
//     kPulsePinChannel[]              physical jacks and which virtual
//                                     engine channel (0..5) each carries
//   kPinClkIn / kPinRstIn /
//     kClkInInverted / kClkInPull     external clock inputs
//   kMidiUartPeriph / kPinMidiTx/Rx   TRS MIDI out UART
//   NEON_MIDI_UART_REGS               the USART instance for ISR TDR writes
// plus board-specific extras (OLED pins on the Seed, LED pins, DAC
// channel for Tempo CV where the DAC is pin-mapped).

#if defined(NEON_BOARD_POD)
#include "board_pins_pod.h"
#elif defined(NEON_BOARD_PATCH_INIT)
#include "board_pins_patch_init.h"
#else
#include "board_pins_seed.h"
#endif
