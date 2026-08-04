#pragma once

#include <cstdint>

namespace halesp {

// Tempo CV via filtered LEDC PWM (HARDWARE.md §5.2: filtered PWM or DAC;
// the DAC option stays open behind this same call shape). ~19.5 kHz,
// 12-bit — far above audio after the RC filter, fine-grained enough for a
// 0–5 V tempo scale.
bool tempo_cv_init(int gpio);

// ratio_q16: 0..65535 spans 0..full-scale output.
void tempo_cv_set_ratio(uint16_t ratio_q16);

}  // namespace halesp
