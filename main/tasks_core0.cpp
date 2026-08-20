// Core 0: networking / application side. Milestone 1 scope: a heartbeat
// placeholder where WiFi + Ableton Link (milestone 2), Ethernet (4), UI (6),
// BLE MIDI (7) and the web editor (8) will attach.

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "oledui/oled_ui.h"
#include "tasks.h"

namespace {

const char* kTag = "app_task";

void app_task(void*) {
  for (;;) {
    ESP_LOGI(kTag, "heartbeat: free heap %lu bytes",
             static_cast<unsigned long>(esp_get_free_heap_size()));
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

}  // namespace

void neon_start_core0_tasks() {
  xTaskCreatePinnedToCore(app_task, "app", 4096, nullptr, 5, nullptr, 0);
#if CONFIG_NEON_BOARD_LINKSYNC
  neon_start_status_led_service();
  neon_start_telemetry_service();
#elif CONFIG_NEON_BOARD_LINKSYNC_EPD
  neon_start_epd_service();
  neon_start_telemetry_service();
#elif CONFIG_NEON_BOARD_LINKSYNC_P4LCD || CONFIG_NEON_BOARD_LINKSYNC_TAB5
  neon_start_lcd_service();
  neon_start_telemetry_service();
#else
  oledui_start();
#endif
}
