#pragma once

#include <cstdint>

namespace halesp {

// NULLLAB / Emakefun I2C GPIO expander (CH32V003 @ 0x24).
// Register map matches github.com/nulllaborg/gpio_expansion_board
// (and the Arduino port emakefun_gpio_expansion_board).
//
// E0–E7 each software-configure as ADC (10-bit, 0..1023), digital I/O
// (pull-up / pull-down / floating), or — E1 and E2 only — PWM.
// The four Grove ports are a passive I2C hub, so the AMYboard OLED
// stays on the same bus.

constexpr uint8_t kGpioExpAddr = 0x24;
constexpr int kGpioExpPins = 8;

// Suggested AMYboard wiring on this board (see docs/AMYBOARD.md).
constexpr int kGpioExpPot = 0;    // E0, 10k B linear pot
constexpr int kGpioExpEncA = 1;   // E1, EC11 A
constexpr int kGpioExpEncB = 2;   // E2, EC11 B
constexpr int kGpioExpEncSw = 3;  // E3, EC11 switch (to GND)

enum class GpioExpMode : uint8_t {
  kInputPullUp = 1 << 0,
  kInputPullDown = 1 << 1,
  kInputFloating = 1 << 2,
  kOutput = 1 << 3,
  kAdc = 1 << 4,
  kPwm = 1 << 5,
};

// Probe 0x24. Idempotent. Does not change pin modes.
bool gpio_exp_init();
bool gpio_exp_present();

bool gpio_exp_set_mode(int pin, GpioExpMode mode);
bool gpio_exp_set_level(int pin, uint8_t level);
bool gpio_exp_get_level(int pin, uint8_t* level);

// 10-bit ADC, 0..1023. Also refreshes the cached reading for this pin.
bool gpio_exp_adc(int pin, uint16_t* value);

// Last successful ADC sample for `pin`, or -1 if none yet.
int gpio_exp_last_adc(int pin);

}  // namespace halesp
