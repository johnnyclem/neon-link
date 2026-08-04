#include "halesp/status_leds.hpp"

#include "driver/gpio.h"

namespace halesp {

namespace {
int g_net = -1;
int g_beat = -1;
int g_run = -1;

bool init_pin(int pin) {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ull << pin;
  io.mode = GPIO_MODE_OUTPUT;
  if (gpio_config(&io) != ESP_OK) {
    return false;
  }
  gpio_set_level(static_cast<gpio_num_t>(pin), 0);
  return true;
}
}  // namespace

bool status_leds_init(int pin_net, int pin_beat, int pin_run) {
  if (!init_pin(pin_net) || !init_pin(pin_beat) || !init_pin(pin_run)) {
    return false;
  }
  g_net = pin_net;
  g_beat = pin_beat;
  g_run = pin_run;
  return true;
}

void status_led_net(bool on) {
  if (g_net >= 0) gpio_set_level(static_cast<gpio_num_t>(g_net), on);
}
void status_led_beat(bool on) {
  if (g_beat >= 0) gpio_set_level(static_cast<gpio_num_t>(g_beat), on);
}
void status_led_run(bool on) {
  if (g_run >= 0) gpio_set_level(static_cast<gpio_num_t>(g_run), on);
}

}  // namespace halesp
