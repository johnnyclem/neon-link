#pragma once

namespace halesp {

// Three panel LEDs: Network, Beat, Run (HARDWARE.md §7.3).
bool status_leds_init(int pin_net, int pin_beat, int pin_run);
void status_led_net(bool on);
void status_led_beat(bool on);
void status_led_run(bool on);

}  // namespace halesp
