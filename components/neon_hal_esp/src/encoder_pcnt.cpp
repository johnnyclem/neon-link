#include "halesp/encoder_pcnt.hpp"

#include <atomic>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/gpio_expansion.hpp"
#include "halesp/i2c_bus.hpp"
#include "neon/input/quadrature.hpp"

namespace halesp {

namespace {
const char* kTag = "encoder";

enum class Backend : uint8_t { kNone, kPcnt, kI2cExp, kM5Unit };

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
std::atomic<int> g_i2c_shorts{0};
std::atomic<int> g_i2c_longs{0};
// Swallow click/long leftovers from I2C noise while the Grove bus and
// the M5 STM32 settle. Rotation still counts.
std::atomic<int64_t> g_mute_press_until_us{0};
std::atomic<bool> g_m5_relax{false};

// Flip if clockwise decrements. Some EC11 footprints swap A/B.
constexpr bool kI2cInvert = false;

// M5Stack Unit Encoder (U135): STM32F030 I2C slave on the Grove hub.
// Register map matches github.com/m5stack/M5Unit-Encoder (write-STOP-read).
constexpr uint8_t kM5EncAddr = 0x40;
constexpr uint8_t kM5RegMode = 0x00;
constexpr uint8_t kM5RegValue = 0x10;
constexpr uint8_t kM5RegButton = 0x20;
constexpr uint8_t kM5RegLed = 0x30;
constexpr int kM5TimeoutMs = 20;
constexpr bool kM5Invert = false;
// STM32 firmware counts both quadrature edges; one mechanical detent is 2.
constexpr int kM5CountsPerDetent = 2;

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
          g_i2c_shorts.fetch_add(1, std::memory_order_relaxed);
        }
      }
      if (!sw_last && !long_fired && now - sw_change_us > kLongUs) {
        long_fired = true;
        g_i2c_longs.fetch_add(1, std::memory_order_relaxed);
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

bool m5_read_value(int16_t* value) {
  if (value == nullptr) {
    return false;
  }
  const uint8_t reg = kM5RegValue;
  uint8_t rd[2] = {};
  if (!i2c_write_stop_read(kM5EncAddr, &reg, 1, rd, 2, kM5TimeoutMs)) {
    return false;
  }
  *value = static_cast<int16_t>(static_cast<uint16_t>(rd[0]) |
                                (static_cast<uint16_t>(rd[1]) << 8));
  return true;
}

bool m5_read_raw_button(uint8_t* raw) {
  if (raw == nullptr) {
    return false;
  }
  const uint8_t reg = kM5RegButton;
  uint8_t v = 0;
  // Official Arduino driver: write register, STOP, then read (not
  // repeated-START). Bit 0 is the switch.
  if (!i2c_write_stop_read(kM5EncAddr, &reg, 1, &v, 1, kM5TimeoutMs)) {
    return false;
  }
  *raw = static_cast<uint8_t>(v & 1);
  return true;
}

void m5_sample_detents(int16_t* last, int* count_rem) {
  int16_t now_val = *last;
  if (!m5_read_value(&now_val)) {
    return;
  }
  int d = static_cast<int>(now_val) - static_cast<int>(*last);
  *last = now_val;
  if (kM5Invert) {
    d = -d;
  }
  *count_rem += d;
  const int steps = *count_rem / kM5CountsPerDetent;
  *count_rem -= steps * kM5CountsPerDetent;
  if (steps != 0) {
    g_i2c_detents.fetch_add(steps, std::memory_order_relaxed);
  }
}

void m5_encoder_task(void*) {
  int16_t last = 0;
  (void)m5_read_value(&last);
  int count_rem = 0;

  // Any 0↔1 change is a click. Press+release is two edges ~100 ms apart;
  // merge those into one short. No polarity, no "held" state, no longs —
  // those were swallowing every click after the double-click that opens
  // the menu.
  uint8_t stable = 0;
  (void)m5_read_raw_button(&stable);
  uint8_t candidate = stable;
  int run = 0;
  int64_t last_short_us = 0;
  unsigned log_div = 0;
  g_i2c_shorts.store(0, std::memory_order_relaxed);
  g_i2c_longs.store(0, std::memory_order_relaxed);

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    (void)g_m5_relax.exchange(false, std::memory_order_relaxed);
    uint8_t raw = candidate;
    const bool got_sw = m5_read_raw_button(&raw);
    m5_sample_detents(&last, &count_rem);
    if (got_sw) {
      if (raw != candidate) {
        candidate = raw;
        run = 1;
      } else if (run < 100) {
        ++run;
      }
      constexpr int kStable = 3;
      // A real press+release on this unit is ~200 ms. 180 ms split one
      // click into two shorts (= double-click → settings).
      constexpr int64_t kMergeUs = 400000;
      if (run == kStable && candidate != stable) {
        stable = candidate;
        const int64_t now = esp_timer_get_time();
        if (last_short_us == 0 || now - last_short_us > kMergeUs) {
          g_i2c_shorts.fetch_add(1, std::memory_order_relaxed);
          last_short_us = now;
          ESP_LOGI(kTag, "m5 click short raw=%u", stable);
        }
      }
    }

    if ((++log_div % 250u) == 0u) {
      ESP_LOGI(kTag, "m5 val=%d sw=%u", static_cast<int>(last), stable);
    }

    vTaskDelayUntil(&wake, pdMS_TO_TICKS(2));
  }
}

bool setup_m5_unit() {
  if (i2c_bus() == nullptr) {
    return false;
  }
  if (!i2c_probe(kM5EncAddr, 50)) {
    return false;
  }
  const uint8_t mode_pkt[2] = {kM5RegMode, 0};  // 0 = Pulse
  if (!i2c_write(kM5EncAddr, mode_pkt, sizeof(mode_pkt), kM5TimeoutMs)) {
    ESP_LOGW(kTag, "M5 Unit Encoder @ 0x40 probe ok but mode write failed");
    return false;
  }
  int16_t v = 0;
  if (!m5_read_value(&v)) {
    ESP_LOGW(kTag, "M5 Unit Encoder @ 0x40 present but value read failed");
    return false;
  }
  g_mute_press_until_us.store(esp_timer_get_time() + 1500000,
                              std::memory_order_relaxed);
  ESP_LOGI(kTag, "M5 Unit Encoder @ 0x40, count=%d", static_cast<int>(v));
  xTaskCreatePinnedToCore(m5_encoder_task, "enc_m5", 3072, nullptr, 10, nullptr,
                          0);
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
  // AMYboard stock: no GPIO encoder. Probe I2C accessories on the front
  // Grove bus — NULLLAB expander first, then M5Stack Unit Encoder (U135).
  if (setup_i2c_exp()) {
    g_backend = Backend::kI2cExp;
    return true;
  }
  if (setup_m5_unit()) {
    g_backend = Backend::kM5Unit;
    return true;
  }
  ESP_LOGW(kTag,
           "no GPIO encoder, no expander @ 0x24, no M5 Unit Encoder @ 0x40 — "
           "panel is display-only");
  g_backend = Backend::kNone;
  g_unit = nullptr;
  g_pin_sw = -1;
  return true;
}

int encoder_take_detents() {
  if (g_backend == Backend::kI2cExp || g_backend == Backend::kM5Unit) {
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
  if (g_backend == Backend::kI2cExp || g_backend == Backend::kM5Unit) {
    const int64_t mute_until =
        g_mute_press_until_us.load(std::memory_order_relaxed);
    if (mute_until != 0 && esp_timer_get_time() < mute_until) {
      g_i2c_shorts.store(0, std::memory_order_relaxed);
      g_i2c_longs.store(0, std::memory_order_relaxed);
      return EncoderPress::kNone;
    }
    const int longs = g_i2c_longs.exchange(0, std::memory_order_relaxed);
    const int shorts = g_i2c_shorts.exchange(0, std::memory_order_relaxed);
    if (longs > 0) {
      return EncoderPress::kLong;
    }
    if (shorts >= 2) {
      return EncoderPress::kDouble;
    }
    if (shorts == 1) {
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

void encoder_clear_press() {
  g_i2c_shorts.store(0, std::memory_order_relaxed);
  g_i2c_longs.store(0, std::memory_order_relaxed);
  g_m5_relax.store(true, std::memory_order_relaxed);
}

int encoder_pot() { return gpio_exp_last_adc(kGpioExpPot); }

}  // namespace halesp
