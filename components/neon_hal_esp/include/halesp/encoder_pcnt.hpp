#pragma once

namespace halesp {

// Quadrature encoder + push button.
//
// pin_a/pin_b >= 0: ESP32 PCNT peripheral (×4 decode; one detent = 4
// counts), switch on pin_sw (polled, active-low).
// pin_a/pin_b < 0: probe the NULLLAB I2C GPIO expander @ 0x24 and, if
// present, sample E1/E2/E3 (EC11) plus E0 (10k pot) over the Grove bus.
bool encoder_init(int pin_a, int pin_b, int pin_sw);

// Detents turned since the last call (signed).
int encoder_take_detents();

// The two gestures the UI distinguishes (DESIGN_SYSTEM.md §11): short press
// enters/confirms/toggles, long press cancels an edit or goes back a level.
enum class EncoderPress : unsigned char {
  kNone,
  kShort,
  kLong,
};

// Debounced press detection; reports each gesture exactly once. Call every
// UI frame. A long press fires as soon as the hold threshold passes rather
// than on release, so the panel reacts under the finger.
EncoderPress encoder_take_press();

// Last 10-bit reading of the expander pot (E0), or -1 if no expander.
int encoder_pot();

}  // namespace halesp
