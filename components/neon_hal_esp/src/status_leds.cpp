#include "halesp/status_leds.hpp"

#include "driver/gpio.h"
#include "driver/ledc.h"

namespace halesp {

namespace {
int g_net = -1;
int g_beat = -1;
int g_run = -1;

bool init_pin(int pin) {
  if (pin < 0) {
    return true;  // disabled
  }
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

namespace {
int g_user = -1;
bool g_user_inverted = false;
bool g_user_pwm = false;
}  // namespace

bool user_led_init(int pin, bool inverted) {
  g_user = pin;
  g_user_inverted = inverted;
  g_user_pwm = false;
  if (pin < 0) {
    return true;
  }
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.timer_num = LEDC_TIMER_1;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.freq_hz = 500;
  timer.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&timer) != ESP_OK) {
    return init_pin(pin);
  }
  ledc_channel_config_t ch = {};
  ch.gpio_num = pin;
  ch.speed_mode = LEDC_LOW_SPEED_MODE;
  ch.channel = LEDC_CHANNEL_1;
  ch.timer_sel = LEDC_TIMER_1;
  ch.intr_type = LEDC_INTR_DISABLE;
  ch.duty = inverted ? 255 : 0;
  if (ledc_channel_config(&ch) != ESP_OK) {
    return init_pin(pin);
  }
  g_user_pwm = true;
  return true;
}

void user_led_duty(uint8_t duty) {
  if (g_user < 0) {
    return;
  }
  if (g_user_pwm) {
    const uint32_t d = g_user_inverted ? (255u - duty) : duty;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, d);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    return;
  }
  const bool on = duty >= 128;
  gpio_set_level(static_cast<gpio_num_t>(g_user),
                 g_user_inverted ? !on : on);
}

}  // namespace halesp
