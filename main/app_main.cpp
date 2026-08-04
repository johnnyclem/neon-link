#include "esp_log.h"
#include "nvs_flash.h"

#include "app_state/config_store.h"
#include "tasks.h"

static const char* kTag = "neon";

extern "C" void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  ESP_LOGI(kTag, "NEON LINK firmware starting");
  neon_config_load();

  // Real-time pulse engine first: the clock path is the product.
  neon_start_core1_tasks();
  // Networking / application side.
  neon_start_link_service();
  neon_start_core0_tasks();
}
