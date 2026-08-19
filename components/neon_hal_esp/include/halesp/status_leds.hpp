#pragma once

#include <cstdint>

namespace halesp {

// Three panel LEDs: Network, Beat, Run (HARDWARE.md §7.3).
bool status_leds_init(int pin_net, int pin_beat, int pin_run);
void status_led_net(bool on);
void status_led_beat(bool on);
void status_led_run(bool on);

// Single user LED (link-sync / XIAO GPIO21). duty is logical 0..255;
// inverted=true means the pin is driven LOW for "on".
bool user_led_init(int pin, bool inverted);
void user_led_duty(uint8_t duty);

}  // namespace halesp
