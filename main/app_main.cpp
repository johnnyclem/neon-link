#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "app_state/config_store.h"
#include "board_pins.h"
#include "halesp/i2c_bus.hpp"
#include "tasks.h"

static const char* kTag = "neon";

extern "C" void app_main(void) {
  // Unconditional early breadcrumb — survives even if the log system is
  // misconfigured; shows up on USB Serial/JTAG before NVS / networking.
  esp_rom_printf("\n[neon] app_main enter free_heap=%u\n",
                 (unsigned)esp_get_free_heap_size());

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

#if CONFIG_NEON_BOARD_AMYBOARD
  ESP_LOGI(kTag, "NEON LINK firmware starting (AMYboard) free_heap=%u",
           (unsigned)esp_get_free_heap_size());
#else
  ESP_LOGI(kTag, "NEON LINK firmware starting (custom PCB)");
#endif

  // Shared I2C early so GP8413 / ADS1015 / OLED all attach to one bus.
  if (kPinI2cSda >= 0 && kPinI2cScl >= 0) {
    if (!halesp::i2c_bus_init(kPinI2cSda, kPinI2cScl)) {
      ESP_LOGW(kTag, "I2C bus init failed; CV / OLED will be unavailable");
    }
  }

  neon_config_load();

  // Real-time pulse engine first: the clock path is the product.
  neon_start_core1_tasks();
  // Networking / application side.
  neon_start_link_service();
  neon_start_midi_service();
  neon_start_core0_tasks();
}
