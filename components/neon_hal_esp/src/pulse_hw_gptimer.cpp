#include "halesp/pulse_hw_gptimer.hpp"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "soc/gpio_reg.h"

namespace halesp {

namespace {
constexpr const char* kTag = "pulse_hw";
constexpr uint64_t kParkIntervalUs = 1000;
}  // namespace

bool PulseHwGptimer::init(const int* gpios, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (gpios[i] < 0 || gpios[i] >= 32) {
      ESP_LOGE(kTag, "pulse GPIO %d out of w1ts range", gpios[i]);
      return false;
    }
    gpio_config_t io = {};
    io.pin_bit_mask = 1ull << gpios[i];
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&io) != ESP_OK) {
      return false;
    }
    gpio_set_level(static_cast<gpio_num_t>(gpios[i]), 0);
  }

  gptimer_config_t cfg = {};
  cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  cfg.direction = GPTIMER_COUNT_UP;
  cfg.resolution_hz = 1000000;  // 1 µs ticks: alarm values are microseconds
  if (gptimer_new_timer(&cfg, &timer_) != ESP_OK) {
    return false;
  }

  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = &PulseHwGptimer::on_alarm;
  if (gptimer_register_event_callbacks(timer_, &cbs, this) != ESP_OK) {
    return false;
  }
  if (gptimer_enable(timer_) != ESP_OK) {
    return false;
  }

  // Correlate the timer count domain with esp_timer once. Both run from the
  // same crystal, so this offset never drifts.
  gptimer_set_raw_count(timer_, 0);
  offset_us_ = esp_timer_get_time();

  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = kParkIntervalUs;
  alarm.reload_count = 0;
  alarm.flags.auto_reload_on_alarm = false;
  if (gptimer_set_alarm_action(timer_, &alarm) != ESP_OK) {
    return false;
  }
  return gptimer_start(timer_) == ESP_OK;
}

bool PulseHwGptimer::submit(const hal::PulseEdge& e) {
  const uint32_t head = head_.load(std::memory_order_relaxed);
  const uint32_t tail = tail_.load(std::memory_order_acquire);
  if (head - tail >= kRingSize) {
    return false;
  }
  ring_[head % kRingSize] = e;
  head_.store(head + 1, std::memory_order_release);
  return true;
}

int64_t PulseHwGptimer::now_us() const { return esp_timer_get_time(); }

bool IRAM_ATTR PulseHwGptimer::on_alarm(gptimer_handle_t timer,
                                        const gptimer_alarm_event_data_t*,
                                        void* user) {
  auto* self = static_cast<PulseHwGptimer*>(user);

  uint64_t now = 0;
  gptimer_get_raw_count(timer, &now);

  uint32_t tail = self->tail_.load(std::memory_order_relaxed);
  const uint32_t head = self->head_.load(std::memory_order_acquire);

  while (tail != head) {
    const hal::PulseEdge& e = self->ring_[tail % kRingSize];
    const uint64_t due =
        static_cast<uint64_t>(e.t_us - self->offset_us_);
    if (due > now) {
      break;
    }
    if (e.gpio_set_mask != 0) {
      REG_WRITE(GPIO_OUT_W1TS_REG, e.gpio_set_mask);
    }
    if (e.gpio_clear_mask != 0) {
      REG_WRITE(GPIO_OUT_W1TC_REG, e.gpio_clear_mask);
    }
    ++tail;
  }
  self->tail_.store(tail, std::memory_order_release);

  gptimer_get_raw_count(timer, &now);
  uint64_t next;
  if (tail != head) {
    next = static_cast<uint64_t>(self->ring_[tail % kRingSize].t_us -
                                 self->offset_us_);
    if (next <= now) {
      next = now + 1;
    }
  } else {
    next = now + kParkIntervalUs;
  }

  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = next;
  alarm.reload_count = 0;
  alarm.flags.auto_reload_on_alarm = false;
  gptimer_set_alarm_action(timer, &alarm);
  return false;
}

}  // namespace halesp
