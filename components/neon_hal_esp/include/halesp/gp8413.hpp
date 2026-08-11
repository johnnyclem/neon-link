#pragma once

#include <cstdint>

namespace halesp {

// DFRobot / Makerfabs GP8413 dual 15-bit DAC over I2C (addr 0x58).
// On AMYboard the output stage maps DAC codes to roughly -10 V .. +10 V
// at the jack (0x0000 -> -10 V, 0x7FFF -> +10 V). The chip must stay in
// its power-up 0–5 V range mode — do not write the range register.
//
// Requires halesp::i2c_bus_init() first.

bool gp8413_init();

// Set one channel to an absolute voltage at the jack (clamped ±10 V).
bool gp8413_set_volts(uint8_t channel /*0|1*/, float volts);

// Q16 ratio (0..65535) mapped linearly onto [lo_v, hi_v].
bool gp8413_set_ratio(uint8_t channel, uint16_t ratio_q16, float lo_v,
                      float hi_v);

}  // namespace halesp
