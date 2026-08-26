#include "halesp/clkin_capture.hpp"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "halesp/ads1015.hpp"
#include "sdkconfig.h"

namespace halesp {

namespace {

const char* kTag = "clkin";
QueueHandle_t g_queue = nullptr;

void IRAM_ATTR on_edge(void* arg) {
  const CaptureEvent ev{
      static_cast<CaptureKind>(reinterpret_cast<intptr_t>(arg)),
      esp_timer_get_time(),
  };
  BaseType_t woken = pdFALSE;
  xQueueSendFromISR(g_queue, &ev, &woken);
  if (woken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

bool config_input(int gpio, CaptureKind kind) {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ull << gpio;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_ENABLE;  // idle low behind the Schmitt
  io.intr_type = GPIO_INTR_POSEDGE;
  if (gpio_config(&io) != ESP_OK) {
    return false;
  }
  return gpio_isr_handler_add(
             static_cast<gpio_num_t>(gpio), &on_edge,
             reinterpret_cast<void*>(static_cast<intptr_t>(kind))) == ESP_OK;
}

#if CONFIG_NEON_BOARD_AMYBOARD
// ADS1015 path: poll both CV inputs and emit rising-edge events with
// hysteresis. Good enough for external tempo following; not sample-accurate.
// Unconnected AMYboard CV jacks idle around 1.2 V. A single 1 V
// threshold treated that as a clock/reset edge at boot. Hysteresis
// matches 5 V modular gates and ignores the floating bias.
constexpr float kRiseVolts = 2.5f;
constexpr float kFallVolts = 1.0f;
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(5);

void ads_poll_task(void*) {
  bool clk_high = false;
  bool rst_high = false;
  for (;;) {
    float v0 = 0, v1 = 0;
    if (ads1015_read_volts(0, &v0)) {
      const bool high = clk_high ? v0 >= kFallVolts : v0 >= kRiseVolts;
      if (high && !clk_high) {
        const CaptureEvent ev{CaptureKind::kClock, esp_timer_get_time()};
        xQueueSend(g_queue, &ev, 0);
      }
      clk_high = high;
    }
    if (ads1015_read_volts(1, &v1)) {
      const bool high = rst_high ? v1 >= kFallVolts : v1 >= kRiseVolts;
      if (high && !rst_high) {
        const CaptureEvent ev{CaptureKind::kReset, esp_timer_get_time()};
        xQueueSend(g_queue, &ev, 0);
      }
      rst_high = high;
    }
    vTaskDelay(kPollTicks);
  }
}
#endif

}  // namespace

bool clkin_capture_init(int clk_gpio, int rst_gpio) {
  g_queue = xQueueCreate(32, sizeof(CaptureEvent));
  if (g_queue == nullptr) {
    return false;
  }

#if CONFIG_NEON_BOARD_AMYBOARD
  // Pins are -1 on AMYboard: use the onboard ADS1015.
  if (clk_gpio < 0 || rst_gpio < 0) {
    if (!ads1015_init()) {
      ESP_LOGW(kTag, "ADS1015 init failed; external clock disabled");
      return false;
    }
    xTaskCreatePinnedToCore(ads_poll_task, "clkin_ads", 3072, nullptr, 3,
                            nullptr, 0);
    ESP_LOGI(kTag, "capturing CLK/RST IN via ADS1015 (CV1/CV2 inputs)");
    return true;
  }
#endif

  if (clk_gpio < 0 || rst_gpio < 0) {
    ESP_LOGI(kTag, "no CLK/RST IN pins on this board");
    return false;
  }

  const esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "isr service install failed: %d", err);
    return false;
  }
  if (!config_input(clk_gpio, CaptureKind::kClock) ||
      !config_input(rst_gpio, CaptureKind::kReset)) {
    ESP_LOGE(kTag, "input config failed");
    return false;
  }
  ESP_LOGI(kTag, "capturing CLK IN on GPIO%d, RST IN on GPIO%d", clk_gpio,
           rst_gpio);
  return true;
}

bool clkin_capture_pop(CaptureEvent* out) {
  if (g_queue == nullptr) {
    return false;
  }
  return xQueueReceive(g_queue, out, 0) == pdTRUE;
}

}  // namespace halesp
