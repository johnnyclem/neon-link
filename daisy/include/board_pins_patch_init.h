#pragma once

// Daisy patch.init() pin map for the NEON LINK headless build (no
// screen; docs/DAISY.md §8). This is the Eurorack-native member of the
// family: the module's own jacks carry the clock I/O with no external
// conditioning needed — gates are 0-5 V both ways and CV OUT is a real
// 0-5 V output.
//
// Panel surface: GATE OUT 1/2 = CLK1/RESET, GATE IN 1/2 = CLK IN /
// RST IN, CV OUT 2 = Tempo CV, button B7 = quantized start/stop,
// toggle B8 = clock source (up = auto-follow CLK IN, down = internal
// only), knob 1 (CV_1) = soft-pickup tempo. Only two gate jacks exist,
// so CLK2-4 and RUN have no physical pin — the audio outs can carry
// clock/reset/run as pulse-as-audio roles (the audio engine's role_l /
// role_r config), which is the intended way to get more clock outputs
// from this panel.

#include "daisy_patch_sm.h"

// --- No encoders on this hardware. -----------------------------------
inline constexpr int kNumEncoders = 0;

// --- Button B7 (index 0). The B8 toggle is a level switch, read
// directly by controls_patch_init.cpp, not a debounced button. --------
inline constexpr int kNumButtons = 1;
inline constexpr daisy::Pin kButtonPins[kNumButtons] = {
    daisy::patch_sm::DaisyPatchSM::B7,
};
inline constexpr daisy::Pin kPinToggle = daisy::patch_sm::DaisyPatchSM::B8;

// --- Pulse outputs: the two gate jacks. Virtual channels 0..5 still
// exist (RUN drives the transport logic and pulse-as-audio); only these
// two land on pins. ----------------------------------------------------
inline constexpr int kNumPulsePins = 2;
inline constexpr daisy::Pin kPulsePins[kNumPulsePins] = {
    daisy::patch_sm::DaisyPatchSM::B5,  // GATE OUT 1 <- CLK1
    daisy::patch_sm::DaisyPatchSM::B6,  // GATE OUT 2 <- RESET
};
inline constexpr uint8_t kPulsePinChannel[kNumPulsePins] = {0, 4};

// --- Inputs: the gate jacks' BJT input stage inverts (pin is LOW while
// the gate is high), and holds the line, so no pull is wanted. --------
inline constexpr daisy::Pin kPinClkIn =
    daisy::patch_sm::DaisyPatchSM::B10;  // GATE IN 1
inline constexpr daisy::Pin kPinRstIn =
    daisy::patch_sm::DaisyPatchSM::B9;  // GATE IN 2
inline constexpr bool kClkInInverted = true;
inline constexpr daisy::GPIO::Pull kClkInPull = daisy::GPIO::Pull::NOPULL;

// --- Tempo CV: CV OUT 2 through the module's own 0-5 V output stage
// (board_daisy.cpp uses WriteCvOut; no DAC channel constant needed).
// CV OUT 1 is left free (it drives the panel LED on stock hardware).

// --- TRS MIDI out: the "UART1" pins on the A header are UART4 on the
// STM32 (A3 = PA0 TX, A2 = PA1 RX). Type A wiring. --------------------
inline constexpr daisy::UartHandler::Config::Peripheral kMidiUartPeriph =
    daisy::UartHandler::Config::Peripheral::UART_4;
inline constexpr daisy::Pin kPinMidiTx = daisy::patch_sm::DaisyPatchSM::A3;
inline constexpr daisy::Pin kPinMidiRx = daisy::patch_sm::DaisyPatchSM::A2;
#define NEON_MIDI_UART_REGS UART4
