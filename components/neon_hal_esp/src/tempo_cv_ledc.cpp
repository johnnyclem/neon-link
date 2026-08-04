#include "halesp/tempo_cv_ledc.hpp"

#include "driver/ledc.h"

namespace halesp {

namespace {
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr uint32_t kResolutionBits = 12;
}  // namespace

bool tempo_cv_init(int gpio) {
  ledc_timer_config_t timer = {};
  timer.speed_mode = kMode;
  timer.duty_resolution = static_cast<ledc_timer_bit_t>(kResolutionBits);
  timer.timer_num = kTimer;
  timer.freq_hz = 19531;  // 80 MHz / 4096
  timer.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&timer) != ESP_OK) {
    return false;
  }

  ledc_channel_config_t ch = {};
  ch.gpio_num = gpio;
  ch.speed_mode = kMode;
  ch.channel = kChannel;
  ch.timer_sel = kTimer;
  ch.duty = 0;
  ch.hpoint = 0;
  return ledc_channel_config(&ch) == ESP_OK;
}

void tempo_cv_set_ratio(uint16_t ratio_q16) {
  const uint32_t max_duty = (1u << kResolutionBits) - 1;
  const uint32_t duty =
      static_cast<uint32_t>(static_cast<uint64_t>(ratio_q16) * max_duty / 65535);
  ledc_set_duty(kMode, kChannel, duty);
  ledc_update_duty(kMode, kChannel);
}

}  // namespace halesp
