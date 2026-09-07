#include "sdkconfig.h"
#include "tasks.h"

#include "app_state/timeline_bus.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/status_leds.hpp"
#include "neon/status_led.hpp"
#include "wifi.h"

#if CONFIG_NEON_BOARD_LINKSYNC || CONFIG_NEON_BOARD_LINKSYNC_C3OLED

namespace {

const char* kTag = "user_led";

#if CONFIG_NEON_BOARD_LINKSYNC
constexpr int64_t kPlayStopDebounceUs = 30000;
constexpr int64_t kPlayStopArmUs = 200000;

void play_stop_init() {
  if (kPinPlayStop < 0) {
    return;
  }
  gpio_config_t io = {};
  io.pin_bit_mask = 1ull << static_cast<unsigned>(kPinPlayStop);
  io.mode = GPIO_MODE_INPUT;
  if (kPlayStopActiveLow) {
    io.pull_up_en = GPIO_PULLUP_ENABLE;
  } else {
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;
  }
  gpio_config(&io);
}

void play_stop_poll(int64_t now_us) {
  if (kPinPlayStop < 0) {
    return;
  }
  const int level = gpio_get_level(static_cast<gpio_num_t>(kPinPlayStop));
  const bool down = kPlayStopActiveLow ? (level == 0) : (level != 0);
  static bool armed = false;
  static bool held = false;
  static int64_t down_us = 0;
  if (!armed) {
    if (!down && now_us >= kPlayStopArmUs) {
      armed = true;
    }
    return;
  }
  if (down && !held) {
    held = true;
    down_us = now_us;
  } else if (!down && held) {
    held = false;
    if (now_us - down_us >= kPlayStopDebounceUs) {
      ControlCommand cmd{};
      cmd.kind = ControlCommand::Kind::kToggle;
      control_queue_push(cmd);
      ESP_LOGI(kTag, "Grove D0: toggle transport");
    }
  }
}
#endif

void led_task(void*) {
  halesp::user_led_init(kPinUserLed, kUserLedInverted);
#if CONFIG_NEON_BOARD_LINKSYNC
  play_stop_init();
#endif
  for (;;) {
    const int64_t now = esp_timer_get_time();
#if CONFIG_NEON_BOARD_LINKSYNC
    play_stop_poll(now);
#endif
    neon::TimelineSnapshot tl{};
    timeline_bus().read(tl);
    const neon::LedPattern pat = neon::classify_led(
        neon_wifi_has_credentials(), neon_wifi_sta_got_ip(), tl.num_peers,
        tl.playing != 0);
    const uint8_t duty = neon::led_duty(pat, tl, now);
    halesp::user_led_duty(duty);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

void neon_start_status_led_service() {
  xTaskCreatePinnedToCore(led_task, "user_led", 3072, nullptr, 4, nullptr, 0);
}

#else

void neon_start_status_led_service() {}

#endif
