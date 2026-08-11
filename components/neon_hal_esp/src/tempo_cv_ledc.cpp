#include "halesp/tempo_cv_ledc.hpp"

#include "driver/ledc.h"
#include "sdkconfig.h"

#if CONFIG_NEON_BOARD_AMYBOARD
#include "board_pins.h"
#include "halesp/gp8413.hpp"
#endif

namespace halesp {

namespace {
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr uint32_t kResolutionBits = 12;
bool g_use_gp8413 = false;
}  // namespace

bool tempo_cv_init(int gpio) {
#if CONFIG_NEON_BOARD_AMYBOARD
  (void)gpio;
  // AMYboard: Tempo CV on GP8413 channel 0 (CV jack 1). 0..5 V maps the
  // configured BPM range (see neon::tempo_cv_ratio_q16).
  g_use_gp8413 = gp8413_init();
  return g_use_gp8413;
#else
  g_use_gp8413 = false;
  if (gpio < 0) {
    return false;
  }
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
#endif
}

void tempo_cv_set_ratio(uint16_t ratio_q16) {
#if CONFIG_NEON_BOARD_AMYBOARD
  if (g_use_gp8413) {
    // 0..5 V tempo scale on CV1 (FEATURES.md must-have #3).
    gp8413_set_ratio(kAmyCvTempoChannel, ratio_q16, 0.0f, 5.0f);
    return;
  }
#endif
  const uint32_t max_duty = (1u << kResolutionBits) - 1;
  const uint32_t duty =
      static_cast<uint32_t>(static_cast<uint64_t>(ratio_q16) * max_duty / 65535);
  ledc_set_duty(kMode, kChannel, duty);
  ledc_update_duty(kMode, kChannel);
}

}  // namespace halesp
