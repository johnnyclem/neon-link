#pragma once

#include <cstdint>

// Tempo CV on the STM32H7's true DAC (channel 1 = PA4 = D23). The
// portable tempo_cv_ratio_q16 mapping lands here as a 12-bit code;
// an op-amp stage scales 0..3.3 V to the 0..5 V jack (HARDWARE.md §5).
namespace tempocv {

void init();

// ratio_q16: 0..0xFFFF over the configured BPM span.
void write_ratio_q16(uint16_t ratio);

}  // namespace tempocv
