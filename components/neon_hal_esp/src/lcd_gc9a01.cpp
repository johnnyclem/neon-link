#include "halesp/lcd_gc9a01.hpp"

#include "sdkconfig.h"

#if CONFIG_NEON_BOARD_LINKSYNC_MATOUCH

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <cstring>

namespace halesp {
namespace {

const char* kTag = "gc9a01";

// The GC9A01 sits on its own SPI2 bus. 40 MHz is comfortably inside the
// panel's rating and leaves margin on the MaTouch flex cable.
constexpr int kSpiHost = SPI2_HOST;
constexpr int kPclkHz = 40 * 1000 * 1000;

// The render framebuffer lives in PSRAM, which the SPI DMA cannot read
// directly. Each flush is copied into this internal DMA-capable bounce
// buffer a band at a time; the completion semaphore keeps a band's DMA
// from being clobbered by the next band's memcpy.
constexpr int kBandRows = 40;

esp_lcd_panel_handle_t g_panel = nullptr;
esp_lcd_panel_io_handle_t g_io = nullptr;
uint16_t* g_bounce = nullptr;
SemaphoreHandle_t g_flush_done = nullptr;

bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t,
                             esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(g_flush_done, &hp);
  return hp == pdTRUE;
}

}  // namespace

bool lcd_gc9a01_init() {
  if (g_panel != nullptr) {
    return true;
  }

  // Backlight low until the first frame is on the panel — GC9A01 shows
  // RAM garbage between reset and the first flush.
  gpio_config_t bl = {};
  bl.pin_bit_mask = 1ull << static_cast<unsigned>(kPinDispBlk);
  bl.mode = GPIO_MODE_OUTPUT;
  gpio_config(&bl);
  gpio_set_level(static_cast<gpio_num_t>(kPinDispBlk), 0);

  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = kPinDispSck;
  buscfg.mosi_io_num = kPinDispMosi;
  buscfg.miso_io_num = -1;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = kGc9a01W * kGc9a01H * 2 + 16;
  esp_err_t err = spi_bus_initialize(static_cast<spi_host_device_t>(kSpiHost),
                                     &buscfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "spi_bus_initialize: %s", esp_err_to_name(err));
    return false;
  }

  esp_lcd_panel_io_spi_config_t io_cfg = {};
  io_cfg.cs_gpio_num = kPinDispCs;
  io_cfg.dc_gpio_num = kPinDispDc;
  io_cfg.spi_mode = 0;
  io_cfg.pclk_hz = kPclkHz;
  io_cfg.trans_queue_depth = 10;
  io_cfg.lcd_cmd_bits = 8;
  io_cfg.lcd_param_bits = 8;
  io_cfg.on_color_trans_done = on_color_done;
  err = esp_lcd_new_panel_io_spi(
      reinterpret_cast<esp_lcd_spi_bus_handle_t>(kSpiHost), &io_cfg, &g_io);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "panel_io_spi: %s", esp_err_to_name(err));
    return false;
  }
  esp_lcd_panel_io_handle_t io = g_io;

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = kPinDispRes;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
  panel_cfg.bits_per_pixel = 16;
  err = esp_lcd_new_panel_gc9a01(io, &panel_cfg, &g_panel);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "new_panel_gc9a01: %s", esp_err_to_name(err));
    g_panel = nullptr;
    return false;
  }

  ESP_ERROR_CHECK(esp_lcd_panel_reset(g_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));
  // GC9A01 ships with colors inverted relative to esp_lcd's assumption.
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(g_panel, true));
  // The MaTouch glass mounts with X flipped vs esp_lcd's default MADCTL,
  // so text reads mirrored until we flip the horizontal axis.
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(g_panel, true, false));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(g_panel, true));

  g_flush_done = xSemaphoreCreateBinary();
  xSemaphoreGive(g_flush_done);  // first band must not block
  g_bounce = static_cast<uint16_t*>(heap_caps_malloc(
      static_cast<size_t>(kGc9a01W) * kBandRows * sizeof(uint16_t),
      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (g_flush_done == nullptr || g_bounce == nullptr) {
    ESP_LOGE(kTag, "no DMA bounce buffer for blit");
    return false;
  }

  ESP_LOGI(kTag, "GC9A01 %dx%d up on SPI%d (cs=%d dc=%d)", kGc9a01W, kGc9a01H,
           kSpiHost, kPinDispCs, kPinDispDc);
  return true;
}

void lcd_gc9a01_backlight(bool on) {
  gpio_set_level(static_cast<gpio_num_t>(kPinDispBlk), on ? 1 : 0);
}

bool lcd_gc9a01_blit(const uint16_t* rgb565, int x, int y, int w, int h) {
  if (g_panel == nullptr || g_bounce == nullptr || rgb565 == nullptr) {
    return false;
  }
  // Copy the PSRAM frame into the internal DMA bounce buffer a band at a
  // time; the SPI DMA can only source from internal RAM. The completion
  // semaphore gates reuse so a band's DMA is never overwritten in flight.
  bool ok = true;
  for (int row = 0; row < h; row += kBandRows) {
    const int rows = (row + kBandRows <= h) ? kBandRows : (h - row);
    // Wait for the previous band's DMA to release the bounce buffer.
    xSemaphoreTake(g_flush_done, portMAX_DELAY);
    std::memcpy(g_bounce, rgb565 + static_cast<size_t>(row) * w,
                static_cast<size_t>(rows) * w * sizeof(uint16_t));
    const esp_err_t err = esp_lcd_panel_draw_bitmap(
        g_panel, x, y + row, x + w, y + row + rows, g_bounce);
    if (err != ESP_OK) {
      // draw_bitmap failed before queueing: the callback will not fire,
      // so hand the token back to keep the next flush from deadlocking.
      xSemaphoreGive(g_flush_done);
      ok = false;
      break;
    }
  }
  // Leave the buffer quiescent: reclaim the last band's token and return
  // it so the next call starts from a known state.
  xSemaphoreTake(g_flush_done, portMAX_DELAY);
  xSemaphoreGive(g_flush_done);
  return ok;
}

}  // namespace halesp

#endif  // CONFIG_NEON_BOARD_LINKSYNC_MATOUCH
