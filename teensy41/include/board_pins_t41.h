#pragma once

// Teensy 4.1 pin map for the NEON LINK touchscreen build target.
//
// Display is the PJRC-standard 2.8" 320x240 SPI module: ILI9341 panel and
// XPT2046 resistive touch controller sharing SPI0 on separate chip
// selects. Wiring follows the PJRC ILI9341 tutorial so an off-the-shelf
// display + Teensy 4.1 combination works without adapters.
//
// Unlike the ESP32-S3 boards there is no Kconfig here — this header is
// the single source of truth for the target (docs/TEENSY41.md mirrors it
// as the wiring table).

// --- 2.8" ILI9341 over SPI0 (SCK=13, MOSI=11, MISO=12) ---------------
inline constexpr int kPinTftCs = 10;
inline constexpr int kPinTftDc = 9;
// Panel reset tied to 3.3 V; 255 tells ILI9341_t3 there is no reset pin.
inline constexpr int kPinTftRst = 255;
// Backlight LED pin, PWM-capable so display_brightness works. Wire the
// module's LED pin here through its onboard transistor/resistor; tie to
// 3.3 V instead if you don't need software brightness (set to -1 then).
inline constexpr int kPinTftBacklight = 24;

// --- XPT2046 touch (same SPI bus, own CS) -----------------------------
inline constexpr int kPinTouchCs = 8;
inline constexpr int kPinTouchIrq = 7;

// --- Rotary encoders (EC11 with push switch, A/B to GND-less GPIO,
// common to GND, internal pullups). ENC1 navigates the menu; ENC2 is the
// performance encoder (tempo / transport). ---------------------------
inline constexpr int kPinEnc1A = 2;
inline constexpr int kPinEnc1B = 3;
inline constexpr int kPinEnc1Sw = 4;
inline constexpr int kPinEnc2A = 5;
inline constexpr int kPinEnc2B = 6;
inline constexpr int kPinEnc2Sw = 14;

// --- Pulse outputs. The edge stream uses virtual channel bits 0..5
// (like the AMYboard build); the timer ISR maps bit i to kPulsePins[i].
// Contiguous bottom-edge block so a single header row serves the jacks.
// 3.3 V logic — level-shift to 5 V for Eurorack (docs/TEENSY41.md §5).
inline constexpr int kPulsePins[6] = {
    33,  // CLK1
    34,  // CLK2
    35,  // CLK3
    36,  // CLK4
    37,  // RESET
    38,  // RUN
};

// --- Inputs (protected 0-5 V gate/trigger, see HARDWARE.md §5.4) ------
inline constexpr int kPinClkIn = 30;
inline constexpr int kPinRstIn = 31;

// --- Status LEDs ------------------------------------------------------
inline constexpr int kPinLedNet = 26;
inline constexpr int kPinLedBeat = 27;
inline constexpr int kPinLedRun = 28;

// --- Tempo CV: FlexPWM pin -> RC filter -> op-amp scale to 0-5 V ------
inline constexpr int kPinTempoCv = 22;

// --- Reserved (not driven by this target yet) -------------------------
// TRS MIDI on Serial1 (TX=1, RX=0) and I2C on 18/19 stay free for the
// MIDI and OLED follow-ups; nothing here may claim those pins.
inline constexpr int kPinMidiTx = 1;
inline constexpr int kPinMidiRx = 0;
