#pragma once

namespace halesp {

// Quadrature encoder on the PCNT peripheral (×4 decode; one detent = 4
// counts) plus the integrated push button (polled, active-low).
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

}  // namespace halesp
