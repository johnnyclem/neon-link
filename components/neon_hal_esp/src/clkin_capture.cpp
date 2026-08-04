#include "halesp/clkin_capture.hpp"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

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

}  // namespace

bool clkin_capture_init(int clk_gpio, int rst_gpio) {
  g_queue = xQueueCreate(32, sizeof(CaptureEvent));
  if (g_queue == nullptr) {
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
