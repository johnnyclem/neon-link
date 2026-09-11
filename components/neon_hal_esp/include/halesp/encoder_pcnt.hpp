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

// PCNT counts per mechanical detent (default 4 = one full ×4 quadrature
// cycle per detent, e.g. KY-040). Encoders that click every half cycle
// (2 counts/detent, e.g. the MaTouch bezel) set this to 2. No effect on
// the I2C encoder backends. Call after encoder_init().
void encoder_set_counts_per_detent(int counts);

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
// UI frame. The GPIO switch is sampled on a 2 ms task (same as I2C) so a
// ~125 ms click survives a slow blit. A long press fires as soon as the
// hold threshold passes rather than on release.
EncoderPress encoder_take_press();

// Drop queued shorts/longs (e.g. right after opening the menu so the
// double-click's leftover hold cannot bounce us back out).
void encoder_clear_press();

// Last 10-bit reading of the expander pot (E0), or -1 if no expander.
int encoder_pot();

}  // namespace halesp
