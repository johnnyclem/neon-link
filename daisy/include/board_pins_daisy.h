#pragma once

// Daisy Seed pin map for the NEON LINK SSD1306/1309 OLED build target.
//
// Pins are named by their Daisy Seed "D" index (the silkscreen GPIO
// numbering, daisy::seed::Dn). Like the Teensy target there is no
// Kconfig — this header is the single source of truth and
// docs/DAISY.md mirrors it as the wiring tables.
//
// Fixed consumers on the Seed that nothing here may claim: the audio
// codec (dedicated pins outside D0..D30), USB micro, the QSPI flash and
// SDRAM (internal), and SWD. D29/D30 double as USB HS D-/D+ on the pin
// header and are left free.

#include "daisy_seed.h"

// --- 128×64 SSD1306/SSD1309 OLED over SPI1 (4-wire mode) --------------
// D9 is SPI1 MISO, but the panel is write-only, so it serves as D/C.
inline constexpr daisy::Pin kPinOledCs = daisy::seed::D7;
inline constexpr daisy::Pin kPinOledSck = daisy::seed::D8;
inline constexpr daisy::Pin kPinOledDc = daisy::seed::D9;
inline constexpr daisy::Pin kPinOledMosi = daisy::seed::D10;
inline constexpr daisy::Pin kPinOledRst = daisy::seed::D11;

// SSD1309 modules (the common 2.42" panel) usually run external VCC:
// set true to skip the SSD1306 charge-pump command in the init sequence.
inline constexpr bool kOledExternalVcc = false;

// --- Rotary encoders (EC11 with push switch, A/B/SW to GPIO, common to
// GND, internal pullups). ENC1 navigates the menu; ENC2 is the
// performance encoder (tempo / transport). ---------------------------
inline constexpr daisy::Pin kPinEnc1A = daisy::seed::D0;
inline constexpr daisy::Pin kPinEnc1B = daisy::seed::D1;
inline constexpr daisy::Pin kPinEnc1Sw = daisy::seed::D2;
inline constexpr daisy::Pin kPinEnc2A = daisy::seed::D3;
inline constexpr daisy::Pin kPinEnc2B = daisy::seed::D4;
inline constexpr daisy::Pin kPinEnc2Sw = daisy::seed::D5;

// --- Pulse outputs. The edge stream uses virtual channel bits 0..5
// (CLK1..4, RESET, RUN — the same scheme as the AMYboard and Teensy
// builds); the timer ISR maps bit i to kPulsePins[i]. Contiguous block
// so a single header row serves the jacks. 3.3 V logic — level-shift to
// 5 V for Eurorack (HARDWARE.md §5). --------------------------------
inline constexpr daisy::Pin kPulsePins[6] = {
    daisy::seed::D15,  // CLK1
    daisy::seed::D16,  // CLK2
    daisy::seed::D17,  // CLK3
    daisy::seed::D18,  // CLK4
    daisy::seed::D19,  // RESET
    daisy::seed::D20,  // RUN
};

// --- Inputs (protected 0-5 V gate/trigger, see HARDWARE.md §5.4) ------
inline constexpr daisy::Pin kPinClkIn = daisy::seed::D21;
inline constexpr daisy::Pin kPinRstIn = daisy::seed::D22;

// --- Tempo CV: the STM32H7's true 12-bit DAC (channel 1 = PA4 = D23),
// op-amp scale to 0-5 V. No RC filter needed — this is a real DAC, not
// PWM like the Teensy build. ------------------------------------------
inline constexpr daisy::Pin kPinTempoCv = daisy::seed::D23;

// --- Status LEDs. No network on this hardware, so the Teensy's NET LED
// becomes an EXT lock indicator (lit while following CLK IN). ---------
inline constexpr daisy::Pin kPinLedBeat = daisy::seed::D24;
inline constexpr daisy::Pin kPinLedRun = daisy::seed::D25;
inline constexpr daisy::Pin kPinLedExt = daisy::seed::D26;

// --- TRS MIDI out: USART1 @ 31250 baud, Type A wiring ----------------
inline constexpr daisy::Pin kPinMidiTx = daisy::seed::D13;
inline constexpr daisy::Pin kPinMidiRx = daisy::seed::D14;  // reserved (MIDI in)

// Free for expansion: D6, D12, D27, D28, D29, D30.
