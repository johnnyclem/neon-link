#include "halesp/encoder_pcnt.hpp"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_timer.h"

namespace halesp {

namespace {
pcnt_unit_handle_t g_unit = nullptr;
int g_count_rem = 0;
int g_pin_sw = -1;
bool g_sw_last = true;  // active-low, idle high
int64_t g_sw_change_us = 0;
}  // namespace

bool encoder_init(int pin_a, int pin_b, int pin_sw) {
  // -1 pins = no hardware encoder (AMYboard stock, or web-only UI).
  if (pin_a < 0 || pin_b < 0) {
    g_unit = nullptr;
    g_pin_sw = -1;
    return true;
  }
  pcnt_unit_config_t unit_cfg = {};
  unit_cfg.high_limit = 32767;
  unit_cfg.low_limit = -32768;
  unit_cfg.flags.accum_count = true;
  if (pcnt_new_unit(&unit_cfg, &g_unit) != ESP_OK) {
    return false;
  }

  pcnt_glitch_filter_config_t filter = {};
  filter.max_glitch_ns = 1000;
  pcnt_unit_set_glitch_filter(g_unit, &filter);

  pcnt_chan_config_t ca = {};
  ca.edge_gpio_num = pin_a;
  ca.level_gpio_num = pin_b;
  pcnt_channel_handle_t ch_a = nullptr;
  if (pcnt_new_channel(g_unit, &ca, &ch_a) != ESP_OK) {
    return false;
  }
  pcnt_channel_set_edge_action(ch_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                               PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(ch_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

  pcnt_chan_config_t cb = {};
  cb.edge_gpio_num = pin_b;
  cb.level_gpio_num = pin_a;
  pcnt_channel_handle_t ch_b = nullptr;
  if (pcnt_new_channel(g_unit, &cb, &ch_b) != ESP_OK) {
    return false;
  }
  pcnt_channel_set_edge_action(ch_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                               PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(ch_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

  if (pcnt_unit_enable(g_unit) != ESP_OK ||
      pcnt_unit_clear_count(g_unit) != ESP_OK ||
      pcnt_unit_start(g_unit) != ESP_OK) {
    return false;
  }

  g_pin_sw = pin_sw;
  gpio_config_t io = {};
  io.pin_bit_mask = 1ull << pin_sw;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  return gpio_config(&io) == ESP_OK;
}

int encoder_take_detents() {
  if (g_unit == nullptr) {
    return 0;
  }
  int count = 0;
  pcnt_unit_get_count(g_unit, &count);
  pcnt_unit_clear_count(g_unit);
  const int total = count + g_count_rem;
  const int detents = total / 4;  // x4 decode per detent
  g_count_rem = total % 4;
  return detents;
}

bool encoder_clicked() {
  if (g_pin_sw < 0) {
    return false;
  }
  const bool level = gpio_get_level(static_cast<gpio_num_t>(g_pin_sw)) != 0;
  const int64_t now = esp_timer_get_time();
  if (level != g_sw_last && now - g_sw_change_us > 20000) {
    g_sw_last = level;
    g_sw_change_us = now;
    return !level;  // active-low: report on press
  }
  return false;
}

}  // namespace halesp
