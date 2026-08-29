#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include <cstdio>
#include <cstring>

#include "ablink/priority.hpp"
#include "app_state/config_store.h"
#include "board_pins.h"
#include "halesp/i2c_bus.hpp"
#include "halesp/lcd_rgb.hpp"
#include "neon/config/model.hpp"
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
#elif CONFIG_NEON_BOARD_P4DEVKIT
  ESP_LOGI(kTag, "NEON LINK firmware starting (P4-Module-DEV-KIT) free_heap=%u",
           (unsigned)esp_get_free_heap_size());
#elif CONFIG_NEON_BOARD_LINKSYNC
  ESP_LOGI(kTag, "link-sync firmware starting (XIAO ESP32S3) free_heap=%u",
           (unsigned)esp_get_free_heap_size());
#elif CONFIG_NEON_BOARD_LINKSYNC_EPD
  ESP_LOGI(kTag, "link-sync firmware starting (5.79 e-Paper) free_heap=%u",
           (unsigned)esp_get_free_heap_size());
#elif CONFIG_NEON_BOARD_LINKSYNC_P4LCD
  ESP_LOGI(kTag, "link-sync firmware starting (P4 5.0 LCD) free_heap=%u",
           (unsigned)esp_get_free_heap_size());
#elif CONFIG_NEON_BOARD_LINKSYNC_TAB5
  ESP_LOGI(kTag, "link-sync firmware starting (Tab5 MIPI LCD) free_heap=%u",
           (unsigned)esp_get_free_heap_size());
#elif CONFIG_NEON_BOARD_LINKSYNC_C3OLED
  ESP_LOGI(kTag, "link-sync firmware starting (C3 0.42 OLED) free_heap=%u",
           (unsigned)esp_get_free_heap_size());
#elif CONFIG_NEON_BOARD_LINKSYNC_RLCD
  ESP_LOGI(kTag, "link-sync firmware starting (RLCD 4.2) free_heap=%u",
           (unsigned)esp_get_free_heap_size());
#else
  ESP_LOGI(kTag, "NEON LINK firmware starting (custom PCB)");
#endif

#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD || CONFIG_NEON_BOARD_LINKSYNC_TAB5
  // CrowPanel: LDO4 3.3 V before C6. Tab5: PI4IOE WIFI_EN + LDO3 DPHY.
  if (!halesp::lcd_rgb_ldos()) {
    ESP_LOGW(kTag, "P4 rails / expander failed");
  }
#endif

  // Shared I2C early so GP8413 / ADS1015 / OLED all attach to one bus.
  if (kPinI2cSda >= 0 && kPinI2cScl >= 0) {
    if (!halesp::i2c_bus_init(kPinI2cSda, kPinI2cScl)) {
      ESP_LOGW(kTag, "I2C bus init failed; CV / OLED will be unavailable");
    }
  }

#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD || CONFIG_NEON_BOARD_LINKSYNC_TAB5
  if (!halesp::lcd_rgb_init()) {
    ESP_LOGW(kTag, "P4 LCD init failed; status task will retry");
  }
#endif

  neon_config_load();

#if CONFIG_NEON_LINKSYNC
  {
    neon::Config cfg = neon_config();
    bool dirty = false;
    if (std::strcmp(cfg.device_name, "neon-link") == 0) {
#if CONFIG_NEON_BOARD_LINKSYNC_EPD
      std::snprintf(cfg.device_name, sizeof(cfg.device_name), "link-epd");
#elif CONFIG_NEON_BOARD_LINKSYNC_P4LCD
      std::snprintf(cfg.device_name, sizeof(cfg.device_name), "link-lcd");
#elif CONFIG_NEON_BOARD_LINKSYNC_TAB5
      std::snprintf(cfg.device_name, sizeof(cfg.device_name), "link-tab");
#elif CONFIG_NEON_BOARD_LINKSYNC_C3OLED
      std::snprintf(cfg.device_name, sizeof(cfg.device_name), "link-c3");
#elif CONFIG_NEON_BOARD_LINKSYNC_MATOUCH
      std::snprintf(cfg.device_name, sizeof(cfg.device_name), "link-mat");
#elif CONFIG_NEON_BOARD_LINKSYNC_RLCD
      std::snprintf(cfg.device_name, sizeof(cfg.device_name), "link-rlcd");
#else
      std::snprintf(cfg.device_name, sizeof(cfg.device_name), "link-sync");
#endif
      dirty = true;
    }
    if (cfg.ble_enabled != 0) {
      cfg.ble_enabled = 0;
      dirty = true;
    }
#if !CONFIG_NEON_AUDIO
    // No audio engine in this build — a stale enabled bit from an image
    // that had one would leave the editor claiming audio that never runs.
    if (cfg.audio.enabled != 0) {
      cfg.audio.enabled = 0;
      dirty = true;
    }
#endif
    if (cfg.telemetry_uart_csv == 0) {
      cfg.telemetry_uart_csv = 1;
      dirty = true;
    }
    if (dirty) {
      neon_config_apply(cfg);
    }
  }
#endif

  // Must land before any task that could touch Link's asio ServiceRunner
  // singleton or start the pump is created (neon_start_link_service /
  // neon_start_audio_service below) — see ablink/priority.hpp. Debug-only:
  // normal boots always resolve to the kFixed defaults these tasks already
  // carried as hardcoded constants.
  ablink::set_link_asio_priority(
      neon::link_asio_task_priority(neon_config().priority_profile));
  ablink::set_link_pump_priority(
      neon::link_pump_task_priority(neon_config().priority_profile));

  // Glass first, and nothing else on the bus yet. The old probe swept
  // every I2C address at a 30 ms timeout; on a cold rail that is 3 s+
  // of CPU0 spin and the 5 s idle-task WDT resets us. After a USB flash
  // the rail was already up so the sweep returned instantly and the
  // panel looked fine — until the next power cycle.
#if !CONFIG_NEON_LINKSYNC
  oledui_bringup();
#endif

  // Real-time pulse engine (and the AMYboard CV mirror, which shares
  // this I2C bus). Panel is already up so a later hang is visible.
  neon_start_core1_tasks();
  neon_start_core0_tasks();
  neon_start_link_service();
#if !CONFIG_NEON_LINKSYNC
  neon_start_midi_service();
#elif CONFIG_NEON_BOARD_LINKSYNC_P4LCD
  // UART1 Crowtail: ClockEngine owns TRS clock; this task drains MIDI IN.
  neon_start_midi_service();
#endif
  // Always linked: web_ui resolves /api/audio/channels against these
  // symbols. On link-sync the service is a no-op (CONFIG_NEON_AUDIO=n).
  neon_start_audio_service();
  // Idles until the user enables OSC in the editor; retries its socket
  // until the network stack is up.
  neon_start_osc_service();
}
