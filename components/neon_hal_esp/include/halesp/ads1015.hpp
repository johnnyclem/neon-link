#pragma once

#include <cstdint>

namespace halesp {

// TI ADS1015 12-bit ADC over I2C (addr 0x48) — the AMYboard CV inputs.
// Requires halesp::i2c_bus_init() first. Readings are in volts at the jack
// using the same bench calibration as the stock AMYboard firmware
// (raw ≈ 20080 + 2003·V, readable window roughly -10 V .. +6.3 V).

bool ads1015_init();

// Blocking single-shot conversion on channel 0 or 1. Returns false on
// I2C error; *volts is unchanged on failure.
bool ads1015_read_volts(uint8_t channel, float* volts);

}  // namespace halesp
