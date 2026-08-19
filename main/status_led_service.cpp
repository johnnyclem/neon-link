#include "sdkconfig.h"
#include "tasks.h"

#include "app_state/timeline_bus.h"
#include "board_pins.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/status_leds.hpp"
#include "neon/status_led.hpp"
#include "wifi.h"

#if CONFIG_NEON_BOARD_LINKSYNC

namespace {

void led_task(void*) {
  halesp::user_led_init(kPinUserLed, kUserLedInverted);
  for (;;) {
    neon::TimelineSnapshot tl{};
    timeline_bus().read(tl);
    const neon::LedPattern pat = neon::classify_led(
        neon_wifi_has_credentials(), neon_wifi_sta_got_ip(), tl.num_peers,
        tl.playing != 0);
    const uint8_t duty = neon::led_duty(pat, tl, esp_timer_get_time());
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
