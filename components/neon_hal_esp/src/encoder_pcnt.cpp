#include "halesp/encoder_pcnt.hpp"

#include <atomic>

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/gpio_expansion.hpp"
#include "neon/input/quadrature.hpp"

namespace halesp {

namespace {
const char* kTag = "encoder";

enum class Backend : uint8_t { kNone, kPcnt, kI2cExp };

Backend g_backend = Backend::kNone;
pcnt_unit_handle_t g_unit = nullptr;
int g_count_rem = 0;
int g_pin_sw = -1;

// GPIO (PCNT) switch debounce — sampled on the UI thread.
bool g_sw_last = true;  // active-low, idle high
int64_t g_sw_change_us = 0;
bool g_long_fired = false;

// I2C expander path: a 4 ms task samples A/B/SW so short clicks and
// detents survive the 100 ms OLED frame. Gestures are queued for the UI.
std::atomic<int> g_i2c_detents{0};
std::atomic<unsigned> g_i2c_press{0};  // bit0 short, bit1 long
constexpr unsigned kPressShort = 1u;
constexpr unsigned kPressLong = 2u;

// Flip if clockwise decrements. Some EC11 footprints swap A/B.
constexpr bool kI2cInvert = false;

bool setup_pcnt(int pin_a, int pin_b, int pin_sw) {
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
  ESP_LOGI(kTag, "GPIO encoder PCNT A=%d B=%d SW=%d", pin_a, pin_b, pin_sw);
  gpio_config_t io = {};
  io.pin_bit_mask = 1ull << pin_sw;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  return gpio_config(&io) == ESP_OK;
}

void i2c_encoder_task(void*) {
  neon::QuadDecoder quad;
  uint8_t a = 1;
  uint8_t b = 1;
  uint8_t sw = 1;
  if (gpio_exp_get_level(kGpioExpEncA, &a) &&
      gpio_exp_get_level(kGpioExpEncB, &b)) {
    unsigned ab = (static_cast<unsigned>(a) << 1) | b;
    if (kI2cInvert) {
      ab = ((ab & 1u) << 1) | ((ab >> 1) & 1u);
    }
    quad.reset(ab);
  }

  bool sw_last = true;
  int64_t sw_change_us = esp_timer_get_time();
  bool long_fired = false;
  unsigned pot_div = 0;
  unsigned log_div = 0;
  uint8_t last_logged_a = 0xff;
  uint8_t last_logged_b = 0xff;
  uint8_t last_logged_sw = 0xff;

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    const bool got_ab = gpio_exp_get_level(kGpioExpEncA, &a) &&
                        gpio_exp_get_level(kGpioExpEncB, &b);
    if (got_ab) {
      unsigned ab = (static_cast<unsigned>(a) << 1) | b;
      if (kI2cInvert) {
        ab = ((ab & 1u) << 1) | ((ab >> 1) & 1u);
      }
      // I2C sampling misses states, so each legal gray step is one menu
      // tick — x4 decode would almost never reach a detent.
      const int d = neon::gray_step(quad.last_ab(), ab);
      quad.reset(ab);
      if (d != 0) {
        g_i2c_detents.fetch_add(d, std::memory_order_relaxed);
      }
    }

    if (gpio_exp_get_level(kGpioExpEncSw, &sw)) {
      constexpr int64_t kDebounceUs = 20000;
      constexpr int64_t kLongUs = 600000;
      const bool level = sw != 0;  // idle high, pressed low
      const int64_t now = esp_timer_get_time();
      if (level != sw_last && now - sw_change_us > kDebounceUs) {
        const bool pressed = !level;
        sw_last = level;
        sw_change_us = now;
        if (pressed) {
          long_fired = false;
        } else if (!long_fired) {
          g_i2c_press.fetch_or(kPressShort, std::memory_order_relaxed);
        }
      }
      if (!sw_last && !long_fired && now - sw_change_us > kLongUs) {
        long_fired = true;
        g_i2c_press.fetch_or(kPressLong, std::memory_order_relaxed);
      }
    }

    if ((++pot_div & 7u) == 0u) {
      uint16_t adc = 0;
      (void)gpio_exp_adc(kGpioExpPot, &adc);
    }

    if ((++log_div % 250u) == 0u || a != last_logged_a || b != last_logged_b ||
        sw != last_logged_sw) {
      ESP_LOGI(kTag, "exp E1/E2/E3 A=%u B=%u SW=%u%s", a, b, sw,
               got_ab ? "" : " (read fail)");
      last_logged_a = a;
      last_logged_b = b;
      last_logged_sw = sw;
    }

    vTaskDelayUntil(&wake, pdMS_TO_TICKS(2));
  }
}

bool setup_i2c_exp() {
  if (!gpio_exp_init()) {
    return false;
  }
  const bool ok =
      gpio_exp_set_mode(kGpioExpPot, GpioExpMode::kAdc) &&
      gpio_exp_set_mode(kGpioExpEncA, GpioExpMode::kInputPullUp) &&
      gpio_exp_set_mode(kGpioExpEncB, GpioExpMode::kInputPullUp) &&
      gpio_exp_set_mode(kGpioExpEncSw, GpioExpMode::kInputPullUp);
  if (!ok) {
    ESP_LOGW(kTag, "expander present but pin setup failed");
    return false;
  }
  uint16_t pot = 0;
  if (gpio_exp_adc(kGpioExpPot, &pot)) {
    ESP_LOGI(kTag,
             "I2C encoder on expander (E1/E2/E3), pot E0=%u/1023", pot);
  } else {
    ESP_LOGI(kTag, "I2C encoder on expander (E1/E2/E3)");
  }
  xTaskCreatePinnedToCore(i2c_encoder_task, "enc_i2c", 3072, nullptr, 4,
                          nullptr, 0);
  return true;
}
}  // namespace

bool encoder_init(int pin_a, int pin_b, int pin_sw) {
  if (pin_a >= 0 && pin_b >= 0) {
    if (!setup_pcnt(pin_a, pin_b, pin_sw)) {
      g_backend = Backend::kNone;
      return false;
    }
    g_backend = Backend::kPcnt;
    return true;
  }
  // AMYboard stock: no GPIO encoder. Fall back to the NULLLAB expander
  // if it's on the front Grove I2C bus.
  if (setup_i2c_exp()) {
    g_backend = Backend::kI2cExp;
    return true;
  }
  ESP_LOGW(kTag, "no GPIO encoder and no expander @ 0x24 — panel is display-only");
  g_backend = Backend::kNone;
  g_unit = nullptr;
  g_pin_sw = -1;
  return true;
}

int encoder_take_detents() {
  if (g_backend == Backend::kI2cExp) {
    return g_i2c_detents.exchange(0, std::memory_order_relaxed);
  }
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

EncoderPress encoder_take_press() {
  if (g_backend == Backend::kI2cExp) {
    const unsigned bits =
        g_i2c_press.exchange(0, std::memory_order_relaxed);
    if (bits & kPressLong) {
      return EncoderPress::kLong;
    }
    if (bits & kPressShort) {
      return EncoderPress::kShort;
    }
    return EncoderPress::kNone;
  }
  if (g_pin_sw < 0) {
    return EncoderPress::kNone;
  }
  constexpr int64_t kDebounceUs = 20000;
  constexpr int64_t kLongUs = 600000;

  const bool level = gpio_get_level(static_cast<gpio_num_t>(g_pin_sw)) != 0;
  const int64_t now = esp_timer_get_time();

  if (level != g_sw_last && now - g_sw_change_us > kDebounceUs) {
    const bool pressed = !level;  // active-low
    g_sw_last = level;
    g_sw_change_us = now;
    if (pressed) {
      g_long_fired = false;
      return EncoderPress::kNone;  // decided on release, or on the timeout
    }
    // Released: a short press only counts if the long one never fired.
    return g_long_fired ? EncoderPress::kNone : EncoderPress::kShort;
  }

  // Still held past the threshold: fire once, under the finger.
  if (!g_sw_last && !g_long_fired && now - g_sw_change_us > kLongUs) {
    g_long_fired = true;
    return EncoderPress::kLong;
  }
  return EncoderPress::kNone;
}

int encoder_pot() { return gpio_exp_last_adc(kGpioExpPot); }

}  // namespace halesp
