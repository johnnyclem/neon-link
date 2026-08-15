#pragma once

// Daisy Pod pin map for the NEON LINK headless build (no screen;
// docs/DAISY.md §8). The Pod's own controls carry the performance
// surface — encoder, two buttons, two RGB LEDs — and the clock I/O
// rides the free Seed GPIO on the Pod's expansion header rows.
//
// Pins the Pod hardware already owns (from libDaisy daisy_pod.cpp):
// encoder D25/D26 + click D13, buttons D27/D28, RGB LEDs
// D17-D20/D23/D24, knob ADCs D15/D21, TRS MIDI IN on USART1 RX (D14),
// and the microSD slot on D1-D6. D13 being the encoder click means
// USART1 TX is unavailable — TRS MIDI OUT moves to UART4 (D12).

#include "daisy_seed.h"

// --- The Pod's encoder (tempo / quantized start-stop). ---------------
inline constexpr int kNumEncoders = 1;
inline constexpr daisy::Pin kEncPins[kNumEncoders][3] = {
    {daisy::seed::D26, daisy::seed::D25, daisy::seed::D13},
};

// --- The Pod's two buttons: 0 = tap tempo, 1 = resync. ---------------
inline constexpr int kNumButtons = 2;
inline constexpr daisy::Pin kButtonPins[kNumButtons] = {
    daisy::seed::D27,  // button 1 (tap)
    daisy::seed::D28,  // button 2 (resync)
};

// --- RGB LEDs (common-anode, active low — libDaisy RgbLed handles the
// inversion). LED1 = beat, LED2 = transport / ext lock. ---------------
inline constexpr daisy::Pin kLed1Pins[3] = {
    daisy::seed::D20, daisy::seed::D19, daisy::seed::D18};  // R, G, B
inline constexpr daisy::Pin kLed2Pins[3] = {
    daisy::seed::D17, daisy::seed::D24, daisy::seed::D23};  // R, G, B

// --- Pulse outputs on the free expansion-header GPIO. Same virtual
// channel scheme as every other target; 3.3 V logic — level-shift for
// Eurorack (HARDWARE.md §5). ------------------------------------------
inline constexpr int kNumPulsePins = 6;
inline constexpr daisy::Pin kPulsePins[kNumPulsePins] = {
    daisy::seed::D0,   // CLK1
    daisy::seed::D7,   // CLK2
    daisy::seed::D8,   // CLK3
    daisy::seed::D9,   // CLK4
    daisy::seed::D10,  // RESET
    daisy::seed::D16,  // RUN
};
inline constexpr uint8_t kPulsePinChannel[kNumPulsePins] = {0, 1, 2, 3, 4, 5};

// --- Inputs. D29/D30 double as USB HS D-/D+ on the Seed header; the
// Pod does not use USB HS, but reclaim these first if that ever
// changes. -------------------------------------------------------------
inline constexpr daisy::Pin kPinClkIn = daisy::seed::D29;
inline constexpr daisy::Pin kPinRstIn = daisy::seed::D30;
inline constexpr bool kClkInInverted = false;
inline constexpr daisy::GPIO::Pull kClkInPull = daisy::GPIO::Pull::PULLDOWN;

// --- Tempo CV: DAC channel 2 (PA5 = D22; channel 1's pin D23 is taken
// by LED2 blue), op-amp scale to 0-5 V. -------------------------------
inline constexpr daisy::Pin kPinTempoCv = daisy::seed::D22;
inline constexpr daisy::DacHandle::Channel kTempoCvDacChannel =
    daisy::DacHandle::Channel::TWO;

// --- TRS MIDI out: UART4 (USART1 TX is the encoder click). D11 is
// UART4 RX, unused. -----------------------------------------------------
inline constexpr daisy::UartHandler::Config::Peripheral kMidiUartPeriph =
    daisy::UartHandler::Config::Peripheral::UART_4;
inline constexpr daisy::Pin kPinMidiTx = daisy::seed::D12;
inline constexpr daisy::Pin kPinMidiRx = daisy::seed::D11;
#define NEON_MIDI_UART_REGS UART4

// --- TRS MIDI in: the Pod's own MIDI IN jack (USART1 RX, D14). A
// separate RX-only UART init; its tx pin stays PORTX so the encoder
// click on the USART1 TX pin is never reconfigured. --------------------
inline constexpr bool kMidiInSharedUart = false;
inline constexpr daisy::UartHandler::Config::Peripheral kMidiInPeriph =
    daisy::UartHandler::Config::Peripheral::USART_1;
inline constexpr daisy::Pin kPinMidiIn = daisy::seed::D14;
#define NEON_MIDI_IN_UART_REGS USART1

// Everything else on the header belongs to the Pod circuit; nothing is
// left free.
