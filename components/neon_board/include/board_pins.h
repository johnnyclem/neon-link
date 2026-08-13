#pragma once

// Board pin map. Selected at build time via Kconfig (NEON_BOARD_*).
//
// CUSTOM: first-pass proposal for a dedicated NEON LINK PCB
//   (HARDWARE.md / docs/ARCHITECTURE.md). Pulse outs are real GPIOs < 32
//   so the GPTimer ISR can write GPIO_OUT_W1TS/W1TC in one register op.
//
// AMYBOARD: shorepine AMYboard (10HP Eurorack, ESP32-S3-WROOM-1).
//   Two ±10 V CV jacks via GP8413 DAC (I2C 0x58), two CV inputs via
//   ADS1015 (I2C 0x48), TRS MIDI, front-panel Grove I2C for OLED.
//   Pulse channels are *virtual* (indices 0..5); the ISR updates an
//   atomic level word and a core-1 task mirrors CLK1 onto CV out 2.
//   Tempo CV rides CV out 1. See docs/AMYBOARD.md.

#include "sdkconfig.h"

// I2S audio (docs/AUDIOLINK.md). The AMYboard LINE jack is a PCM3060
// (I2S slave, 32-bit slots, 256fs MCLK) on the same pins as
// tulip/amyboard/pins.h. Kconfig carries the numbers so a custom board
// can override; AMYBOARD defaults are the real map. -1 leaves the
// codecs idle and the editor says why.
#ifdef CONFIG_NEON_I2S_MCLK
inline constexpr int kPinI2sMclk = CONFIG_NEON_I2S_MCLK;
inline constexpr int kPinI2sBclk = CONFIG_NEON_I2S_BCLK;
inline constexpr int kPinI2sLrclk = CONFIG_NEON_I2S_LRCLK;
inline constexpr int kPinI2sDout = CONFIG_NEON_I2S_DOUT;
inline constexpr int kPinI2sDin = CONFIG_NEON_I2S_DIN;
#else
inline constexpr int kPinI2sMclk = -1;
inline constexpr int kPinI2sBclk = -1;
inline constexpr int kPinI2sLrclk = -1;
inline constexpr int kPinI2sDout = -1;
inline constexpr int kPinI2sDin = -1;
#endif

#if CONFIG_NEON_BOARD_AMYBOARD

// Virtual pulse channels (not real GPIOs). Bits 0..5 in the pulse level
// word map to CLK1..4 / RESET / RUN. The hardware only has two CV jacks:
//   CV1 = Tempo CV (GP8413 ch0)
//   CV2 = primary clock / gate from CLK1 (GP8413 ch1)
inline constexpr int kPinClk1 = 0;
inline constexpr int kPinClk2 = 1;
inline constexpr int kPinClk3 = 2;
inline constexpr int kPinClk4 = 3;
inline constexpr int kPinReset = 4;
inline constexpr int kPinRun = 5;
inline constexpr bool kPulseVirtual = true;

// Tempo CV is driven through the GP8413, not LEDC PWM. kPinTempoCv is
// unused by the LEDC path and kept only so call sites compile.
inline constexpr int kPinTempoCv = -1;

// TRS MIDI out (Type A default on AMYboard). UART1 @ 31250 baud.
// Pin 14 = Type A, pin 15 = Type B (see amyboard.set_midi_type).
inline constexpr int kPinMidiTx = 14;
inline constexpr int kPinMidiRx = 21;

// Clock / reset inputs come from the ADS1015 ADC, not GPIO.
// Sentinel -1 means "use ADS1015 path" in clkin_capture_init.
inline constexpr int kPinClkIn = -1;
inline constexpr int kPinRstIn = -1;

// No onboard W5500. ethernet_start() short-circuits when CS is -1.
inline constexpr int kPinEthSclk = -1;
inline constexpr int kPinEthMosi = -1;
inline constexpr int kPinEthMiso = -1;
inline constexpr int kPinEthCs = -1;
inline constexpr int kPinEthInt = -1;
inline constexpr int kPinEthRst = -1;

// Front-panel Grove I2C (I2C_NUM_0 master). Shared by OLED, GP8413, ADS1015.
inline constexpr int kPinI2cSda = 17;
inline constexpr int kPinI2cScl = 18;

// Optional SPI 128×128 SH1107 — disabled by default. AMYboard stock path is
// I2C Grove (ssd1327 @ 0x3d / sh1107 @ 0x3c), same as amyboard.init_display().
// Enable only if you have a true SPI panel: SCK=12 MOSI=11 CS=10 DC=7.
inline constexpr int kPinDispSck = -1;
inline constexpr int kPinDispMosi = -1;
inline constexpr int kPinDispCs = -1;
inline constexpr int kPinDispDc = -1;
inline constexpr int kPinDispRes = -1;

// No dedicated GPIO encoder / status LEDs on stock AMYboard. encoder_init
// falls back to the NULLLAB I2C GPIO expander @ 0x24 (E0 pot, E1/E2/E3
// EC11) when these pins are -1. -1 = no native GPIO.
inline constexpr int kPinEncA = -1;
inline constexpr int kPinEncB = -1;
inline constexpr int kPinEncSw = -1;
inline constexpr int kPinLedNet = -1;
inline constexpr int kPinLedBeat = -1;
inline constexpr int kPinLedRun = -1;

// GP8413 DAC (I2C) channel map for the two CV jacks.
inline constexpr int kAmyCvTempoChannel = 0;  // CV1
inline constexpr int kAmyCvClockChannel = 1;  // CV2
inline constexpr float kAmyGateHighVolts = 5.0f;
inline constexpr float kAmyGateLowVolts = 0.0f;

#else  // CONFIG_NEON_BOARD_CUSTOM (default)

// Proposed ESP32-S3-WROOM-1 pinout for a dedicated NEON LINK PCB.
// Constraints: pulse outs < GPIO32, no strapping pins (0, 3, 45, 46),
// no flash/PSRAM pins (26–37 on octal-PSRAM WROOM variants).

inline constexpr int kPinClk1 = 4;
inline constexpr int kPinClk2 = 5;
inline constexpr int kPinClk3 = 6;
inline constexpr int kPinClk4 = 7;
inline constexpr int kPinReset = 15;
inline constexpr int kPinRun = 16;
inline constexpr bool kPulseVirtual = false;

inline constexpr int kPinTempoCv = 17;  // LEDC PWM -> RC filter
inline constexpr int kPinMidiTx = 18;   // UART1, 31250 baud, TRS Type A
inline constexpr int kPinMidiRx = -1;

inline constexpr int kPinClkIn = 8;
inline constexpr int kPinRstIn = 9;

inline constexpr int kPinEthSclk = 12;
inline constexpr int kPinEthMosi = 11;
inline constexpr int kPinEthMiso = 13;
inline constexpr int kPinEthCs = 10;
inline constexpr int kPinEthInt = 14;
inline constexpr int kPinEthRst = 21;

inline constexpr int kPinI2cSda = 47;
inline constexpr int kPinI2cScl = 48;

// No SPI panel on the custom PCB profile by default.
inline constexpr int kPinDispSck = -1;
inline constexpr int kPinDispMosi = -1;
inline constexpr int kPinDispCs = -1;
inline constexpr int kPinDispDc = -1;
inline constexpr int kPinDispRes = -1;

inline constexpr int kPinEncA = 39;
inline constexpr int kPinEncB = 40;
inline constexpr int kPinEncSw = 41;
inline constexpr int kPinLedNet = 42;
inline constexpr int kPinLedBeat = 2;
inline constexpr int kPinLedRun = 1;

inline constexpr int kAmyCvTempoChannel = 0;
inline constexpr int kAmyCvClockChannel = 1;
inline constexpr float kAmyGateHighVolts = 5.0f;
inline constexpr float kAmyGateLowVolts = 0.0f;

#endif
