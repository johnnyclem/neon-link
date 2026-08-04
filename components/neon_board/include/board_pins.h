#pragma once

// Proposed ESP32-S3-WROOM-1 pinout for NEON LINK. This is the software
// team's first-pass proposal (HARDWARE.md §13 deliverable input) — subject
// to hardware review. Constraints honored:
//  - all pulse outputs < GPIO32 (single-register w1ts/w1tc writes)
//  - no strapping pins (0, 3, 45, 46)
//  - no flash/PSRAM pins (26–37 on octal-PSRAM WROOM variants)
// Full table with rationale: docs/ARCHITECTURE.md.

// Pulse outputs (5 V level-shifted on the board)
inline constexpr int kPinClk1 = 4;
inline constexpr int kPinClk2 = 5;
inline constexpr int kPinClk3 = 6;
inline constexpr int kPinClk4 = 7;
inline constexpr int kPinReset = 15;
inline constexpr int kPinRun = 16;

// Analog / serial outputs
inline constexpr int kPinTempoCv = 17;  // LEDC PWM -> RC filter
inline constexpr int kPinMidiTx = 18;   // UART1, 31250 baud, TRS Type A

// Inputs (Schmitt-conditioned on the board)
inline constexpr int kPinClkIn = 8;
inline constexpr int kPinRstIn = 9;

// W5500 Ethernet (SPI2)
inline constexpr int kPinEthSclk = 12;
inline constexpr int kPinEthMosi = 11;
inline constexpr int kPinEthMiso = 13;
inline constexpr int kPinEthCs = 10;
inline constexpr int kPinEthInt = 14;
inline constexpr int kPinEthRst = 21;

// OLED I2C
inline constexpr int kPinI2cSda = 47;
inline constexpr int kPinI2cScl = 48;

// Encoder + status LEDs
inline constexpr int kPinEncA = 39;
inline constexpr int kPinEncB = 40;
inline constexpr int kPinEncSw = 41;
inline constexpr int kPinLedNet = 42;
inline constexpr int kPinLedBeat = 2;
inline constexpr int kPinLedRun = 1;
