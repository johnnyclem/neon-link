#pragma once

namespace halesp {

// Quadrature encoder + push button.
//
// pin_a/pin_b >= 0: ESP32 PCNT peripheral (×4 decode; one detent = 4
// counts), switch on pin_sw (polled, active-low).
// pin_a/pin_b < 0: probe Grove I2C accessories. NULLLAB expander @ 0x24
// first (E1/E2/E3 EC11 + E0 pot); then M5Stack Unit Encoder (U135) @ 0x40.
bool encoder_init(int pin_a, int pin_b, int pin_sw);

// Detents turned since the last call (signed).
int encoder_take_detents();

// Gestures the UI distinguishes (DESIGN_SYSTEM.md §11). kDouble is two
// shorts inside one UI frame; the OLED task also treats two shorts
// within 400 ms as a double-click.
enum class EncoderPress : unsigned char {
  kNone,
  kShort,
  kLong,
  kDouble,
};

// Debounced press detection; reports each gesture exactly once. Call every
// UI frame. A long press fires as soon as the hold threshold passes rather
// than on release, so the panel reacts under the finger.
EncoderPress encoder_take_press();

// Drop queued shorts/longs (e.g. right after opening the menu so the
// double-click's leftover hold cannot bounce us back out).
void encoder_clear_press();

// Last 10-bit reading of the expander pot (E0), or -1 if no expander.
int encoder_pot();

}  // namespace halesp
