#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "app_state/config_store.h"
#include "board_pins.h"
#include "halesp/i2c_bus.hpp"
#include "oledui/oled_ui.h"
#include "tasks.h"

static const char* kTag = "neon";

extern "C" void app_main(void) {
  // Unconditional early breadcrumb — survives even if the log system is
  // misconfigured; shows up on USB Serial/JTAG before NVS / networking.
  esp_rom_printf("\n[neon] app_main enter free_heap=%u\n",
                 (unsigned)esp_get_free_heap_size());

  // Confirm BEFORE NVS or anything that can fail. A USB flash boots the
  // new image as ESP_OTA_IMG_PENDING_VERIFY. Skip this call and the next
  // reset loads the other OTA slot — empty after a USB flash, which is
  // the blank OLED with two pixels lit. Not gated on Kconfig: if the
  // bootloader has rollback on and the app was built with it off, the
  // #if used to compile this out and the brick came back. INVALID_STATE
  // means rollback is off or we already confirmed; that is fine.
  {
    const esp_err_t ota_err = esp_ota_mark_app_valid_cancel_rollback();
    if (ota_err == ESP_OK) {
      esp_rom_printf("[neon] OTA image marked valid\n");
    } else if (ota_err != ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
      esp_rom_printf("[neon] esp_ota_mark_app_valid: %s\n",
                     esp_err_to_name(ota_err));
    }
  }

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

  // Glass first, and nothing else on the bus yet. The old probe swept
  // every I2C address at a 30 ms timeout; on a cold rail that is 3 s+
  // of CPU0 spin and the 5 s idle-task WDT resets us. After a USB flash
  // the rail was already up so the sweep returned instantly and the
  // panel looked fine — until the next power cycle.
  oledui_bringup();

  // Real-time pulse engine (and the AMYboard CV mirror, which shares
  // this I2C bus). Panel is already up so a later hang is visible.
  neon_start_core1_tasks();
  neon_start_core0_tasks();
  neon_start_link_service();
  neon_start_midi_service();
  neon_start_audio_service();
}
