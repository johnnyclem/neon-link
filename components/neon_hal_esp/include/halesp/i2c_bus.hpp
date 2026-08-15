#pragma once

#include "driver/i2c_master.h"

namespace halesp {

// Shared I2C master bus (I2C_NUM_0). On AMYboard this carries the front-
// panel Grove accessories (OLED), the onboard GP8413 CV DAC, and the
// onboard ADS1015 CV ADC. Idempotent: first caller wins on pin config.
bool i2c_bus_init(int sda_gpio, int scl_gpio, uint32_t hz = 400000);

i2c_master_bus_handle_t i2c_bus();

// All transfers take a bus mutex — safe from ads1015 / gp8413 / oled tasks.

// ACK-only probe (no payload). Preferred for bus scans.
bool i2c_probe(uint8_t addr7, int timeout_ms = 50);

bool i2c_write(uint8_t addr7, const uint8_t* data, size_t len,
               int timeout_ms = 50);
bool i2c_write_read(uint8_t addr7, const uint8_t* wr, size_t wr_len,
                    uint8_t* rd, size_t rd_len, int timeout_ms = 50);
// Write, STOP, then read. The NULLLAB expander (and some other Grove
// slaves) do not implement repeated-START register reads.
bool i2c_write_stop_read(uint8_t addr7, const uint8_t* wr, size_t wr_len,
                         uint8_t* rd, size_t rd_len, int timeout_ms = 50);

}  // namespace halesp
