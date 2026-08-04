#pragma once

namespace halesp {

// Quadrature encoder on the PCNT peripheral (×4 decode; one detent = 4
// counts) plus the integrated push button (polled, active-low).
bool encoder_init(int pin_a, int pin_b, int pin_sw);

// Detents turned since the last call (signed).
int encoder_take_detents();

// Debounced click detection; true once per press. Call every UI frame.
bool encoder_clicked();

}  // namespace halesp
