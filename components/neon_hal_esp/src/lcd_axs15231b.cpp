#include "halesp/lcd_axs15231b.hpp"

#include "sdkconfig.h"

#if CONFIG_NEON_BOARD_LINKSYNC_JC3248

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "halesp/i2c_bus.hpp"

#include <cstring>

namespace halesp {
namespace {

const char* kTag = "axs15231b";

// The panel sits on SPI2 in quad (QSPI) mode. 40 MHz matches the vendor
// BSP and is inside the AXS15231B rating.
constexpr int kSpiHost = SPI2_HOST;

// Board init sequence, ported verbatim from the working JC3248W535EN
// DEMO_LVGL BSP (src/esp_bsp.c). This is what makes THIS panel come up
// correctly instead of the driver's generic default. Ends at SLPOUT
// (0x11, 120 ms) + RAMWR (0x2C); display-on is driven separately.
const axs15231b_lcd_init_cmd_t kInitCmds[] = {
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5}, 8, 0},
    {0xA0, (uint8_t[]){0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F, 0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00}, 17, 0},
    {0xA2, (uint8_t[]){0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0, 0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xf9, 0x10, 0x02, 0xff, 0xff, 0xF0, 0x90, 0x01, 0x32, 0xA0, 0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A}, 31, 0},
    {0xD0, (uint8_t[]){0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01, 0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00, 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12}, 30, 0},
    {0xA3, (uint8_t[]){0xA0, 0x06, 0xAa, 0x00, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55}, 22, 0},
    {0xC1, (uint8_t[]){0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0d, 0x00, 0xFF, 0x40}, 30, 0},
    {0xC3, (uint8_t[]){0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01}, 11, 0},
    {0xC4, (uint8_t[]){0x00, 0x24, 0x33, 0x80, 0x00, 0xea, 0x64, 0x32, 0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50}, 29, 0},
    {0xC5, (uint8_t[]){0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20, 0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F, 0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00}, 23, 0},
    {0xC6, (uint8_t[]){0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22}, 20, 0},
    {0xC7, (uint8_t[]){0x50, 0x32, 0x28, 0x00, 0xa2, 0x80, 0x8f, 0x00, 0x80, 0xff, 0x07, 0x11, 0x9c, 0x67, 0xff, 0x24, 0x0c, 0x0d, 0x0e, 0x0f}, 20, 0},
    {0xC9, (uint8_t[]){0x33, 0x44, 0x44, 0x01}, 4, 0},
    {0xCF, (uint8_t[]){0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08, 0x12, 0xA0, 0x08}, 27, 0},
    {0xD5, (uint8_t[]){0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74, 0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46, 0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00, 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00}, 30, 0},
    {0xD6, (uint8_t[]){0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x84, 0x00, 0x20, 0x01, 0x00}, 30, 0},
    {0xD7, (uint8_t[]){0x03, 0x01, 0x0b, 0x09, 0x0f, 0x0d, 0x1E, 0x1F, 0x18, 0x1d, 0x1f, 0x19, 0x40, 0x8E, 0x04, 0x00, 0x20, 0xA0, 0x1F}, 19, 0},
    {0xD8, (uint8_t[]){0x02, 0x00, 0x0a, 0x08, 0x0e, 0x0c, 0x1E, 0x1F, 0x18, 0x1d, 0x1f, 0x19}, 12, 0},
    {0xD9, (uint8_t[]){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 12, 0},
    {0xDD, (uint8_t[]){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 12, 0},
    {0xDF, (uint8_t[]){0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90}, 8, 0},
    {0xE0, (uint8_t[]){0x3B, 0x28, 0x10, 0x16, 0x0c, 0x06, 0x11, 0x28, 0x5c, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D}, 17, 0},
    {0xE1, (uint8_t[]){0x37, 0x28, 0x10, 0x16, 0x0b, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F}, 17, 0},
    {0xE2, (uint8_t[]){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 17, 0},
    {0xE3, (uint8_t[]){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F}, 17, 0},
    {0xE4, (uint8_t[]){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 17, 0},
    {0xE5, (uint8_t[]){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F}, 17, 0},
    {0xA4, (uint8_t[]){0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80, 0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30}, 16, 0},
    {0xA4, (uint8_t[]){0x85, 0x85, 0x95, 0x85}, 4, 0},
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, 0},
    {0x13, (uint8_t[]){0x00}, 0, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x2C, (uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4, 0},
};

esp_lcd_panel_handle_t g_panel = nullptr;
esp_lcd_panel_io_handle_t g_io = nullptr;
SemaphoreHandle_t g_flush_done = nullptr;
bool g_shown = false;

// AXS15231B cap-touch via the component's esp_lcd_touch driver on the
// LEGACY I2C driver — the exact stack the vendor demo / factory firmware
// use. neon's new-API master bus ACKed but returned all-zero reports here,
// so this board keeps I2C to itself (app_main skips the shared bus).
esp_lcd_touch_handle_t g_touch = nullptr;

bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t,
                             esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(g_flush_done, &hp);
  return hp == pdTRUE;
}

}  // namespace

bool lcd_axs15231b_init() {
  if (g_panel != nullptr) {
    return true;
  }

  // Backlight off until the first frame is up (the panel shows GRAM
  // garbage between reset and the first flush).
  gpio_config_t bl = {};
  bl.pin_bit_mask = 1ull << static_cast<unsigned>(kPinDispBlk);
  bl.mode = GPIO_MODE_OUTPUT;
  gpio_config(&bl);
  gpio_set_level(static_cast<gpio_num_t>(kPinDispBlk), 0);

  // Hand-expanded QSPI configs. The component's AXS15231B_PANEL_BUS_QSPI_
  // CONFIG / _IO_QSPI_CONFIG macros use C-style designated initializers in
  // an order C++ rejects, so build the structs by assignment instead.
  const int max_xfer = kAxsW * kAxsH * 2 + 64;  // one full RGB565 frame
  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = kPinDispSck;
  buscfg.data0_io_num = kPinDispMosi;  // QSPI D0
  buscfg.data1_io_num = kPinQspiD1;
  buscfg.data2_io_num = kPinQspiD2;
  buscfg.data3_io_num = kPinQspiD3;
  buscfg.max_transfer_sz = max_xfer;
  esp_err_t err = spi_bus_initialize(static_cast<spi_host_device_t>(kSpiHost),
                                     &buscfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "spi_bus_initialize: %s", esp_err_to_name(err));
    return false;
  }

  esp_lcd_panel_io_spi_config_t io_cfg = {};
  io_cfg.cs_gpio_num = kPinDispCs;
  io_cfg.dc_gpio_num = -1;  // QSPI: no DC line
  io_cfg.spi_mode = 3;
  io_cfg.pclk_hz = 40 * 1000 * 1000;
  io_cfg.trans_queue_depth = 10;
  io_cfg.on_color_trans_done = on_color_done;
  io_cfg.user_ctx = nullptr;
  io_cfg.lcd_cmd_bits = 32;
  io_cfg.lcd_param_bits = 8;
  io_cfg.flags.quad_mode = true;
  err = esp_lcd_new_panel_io_spi(
      reinterpret_cast<esp_lcd_spi_bus_handle_t>(kSpiHost), &io_cfg, &g_io);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "panel_io_spi: %s", esp_err_to_name(err));
    return false;
  }

  axs15231b_vendor_config_t vendor_cfg = {};
  vendor_cfg.init_cmds = kInitCmds;
  vendor_cfg.init_cmds_size = sizeof(kInitCmds) / sizeof(kInitCmds[0]);
  vendor_cfg.flags.use_qspi_interface = 1;
  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = kPinDispRes;  // -1: software SWRESET
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.bits_per_pixel = 16;
  panel_cfg.vendor_config = &vendor_cfg;
  err = esp_lcd_new_panel_axs15231b(g_io, &panel_cfg, &g_panel);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "new_panel_axs15231b: %s", esp_err_to_name(err));
    g_panel = nullptr;
    return false;
  }

  ESP_ERROR_CHECK(esp_lcd_panel_reset(g_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));
  // Leave the panel dark until the first frame lands (matches the demo).
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(g_panel, false));

  g_flush_done = xSemaphoreCreateBinary();  // starts empty; on_color gives
  if (g_flush_done == nullptr) {
    ESP_LOGE(kTag, "no flush semaphore");
    return false;
  }

  ESP_LOGI(kTag, "AXS15231B %dx%d up on QSPI%d (cs=%d sck=%d)", kAxsW, kAxsH,
           kSpiHost, kPinDispCs, kPinDispSck);
  return true;
}

namespace {

bool g_bl_pwm = false;

bool backlight_pwm_init() {
  if (g_bl_pwm) {
    return true;
  }
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_10_BIT;
  timer.timer_num = LEDC_TIMER_1;
  timer.freq_hz = 20000;
  timer.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&timer) != ESP_OK) {
    return false;
  }
  ledc_channel_config_t ch = {};
  ch.gpio_num = kPinDispBlk;
  ch.speed_mode = LEDC_LOW_SPEED_MODE;
  ch.channel = LEDC_CHANNEL_0;
  ch.timer_sel = LEDC_TIMER_1;
  ch.duty = 0;
  ch.hpoint = 0;
  if (ledc_channel_config(&ch) != ESP_OK) {
    return false;
  }
  g_bl_pwm = true;
  return true;
}

}  // namespace

void lcd_axs15231b_backlight(bool on) {
  lcd_axs15231b_backlight_level(on ? 255 : 0);
}

void lcd_axs15231b_backlight_level(uint8_t level) {
  if (!backlight_pwm_init()) {
    gpio_set_level(static_cast<gpio_num_t>(kPinDispBlk), level != 0 ? 1 : 0);
    return;
  }
  const uint32_t duty = (static_cast<uint32_t>(level) * 1023u) / 255u;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

bool lcd_axs15231b_blit(const uint16_t* rgb565, int x, int y, int w, int h) {
  if (g_panel == nullptr || rgb565 == nullptr) {
    return false;
  }
  // Stream the frame as ~30 KB horizontal bands top-to-bottom, straight
  // from PSRAM (the S3 GDMA reads external RAM), exactly like the vendor
  // LVGL demo's full-refresh flush. One 300 KB transfer overruns the SPI
  // color path ("send color data failed"); banded transfers do not. The
  // driver keys RAMWR (first band, y==0) vs RAMWRC (continuation) off
  // y_start, so the bands must go in order from row 0. g_flush_done starts
  // empty and is given by on_color_done, so each Take waits that band out.
  constexpr int kBand = 48;  // 48 * 320 * 2 ≈ 30 KB
  for (int row = 0; row < h; row += kBand) {
    const int rows = (row + kBand <= h) ? kBand : (h - row);
    const esp_err_t err = esp_lcd_panel_draw_bitmap(
        g_panel, x, y + row, x + w, y + row + rows,
        rgb565 + static_cast<size_t>(row) * w);
    if (err != ESP_OK) {
      return false;
    }
    // Bounded wait: a missed completion callback must not freeze the whole
    // display task forever (that manifested as a blank/frozen panel). Drop
    // the band and keep the loop alive if the DMA-done signal never comes.
    if (xSemaphoreTake(g_flush_done, pdMS_TO_TICKS(100)) != pdTRUE) {
      static unsigned to = 0;
      if ((to++ % 30) == 0) {
        ESP_LOGW(kTag, "blit completion wait timed out (band y=%d)", y + row);
      }
    }
  }

  // Reveal the panel once the first frame has landed.
  if (!g_shown) {
    esp_lcd_panel_disp_on_off(g_panel, true);
    g_shown = true;
  }
  return true;
}

bool lcd_axs15231b_touch_init(int sda, int scl) {
  if (g_touch != nullptr) {
    return true;
  }
  // Share neon's new-API master bus (app_main brought it up; idempotent),
  // then drive the touch with the component's own esp_lcd_touch reader on
  // the v2 (bus-handle) IO. This is the vendor read logic on the driver
  // neon standardizes on — no legacy/driver_ng conflict.
  if (!i2c_bus_init(sda, scl)) {
    ESP_LOGW(kTag, "touch i2c bus init failed");
    return false;
  }
  i2c_master_bus_handle_t bus = i2c_bus();
  if (bus == nullptr) {
    ESP_LOGE(kTag, "no i2c master bus handle");
    return false;
  }

  esp_lcd_panel_io_i2c_config_t tio_cfg = {};
  tio_cfg.dev_addr = 0x3B;
  tio_cfg.scl_speed_hz = 400000;
  tio_cfg.control_phase_bytes = 1;
  tio_cfg.dc_bit_offset = 0;
  tio_cfg.lcd_cmd_bits = 8;
  tio_cfg.flags.disable_control_phase = 1;
  esp_lcd_panel_io_handle_t tio = nullptr;
  esp_err_t err = esp_lcd_new_panel_io_i2c_v2(bus, &tio_cfg, &tio);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "touch io_i2c: %s", esp_err_to_name(err));
    return false;
  }

  esp_lcd_touch_config_t tp_cfg = {};
  tp_cfg.x_max = kAxsW;
  tp_cfg.y_max = kAxsH;
  tp_cfg.rst_gpio_num = GPIO_NUM_NC;
  tp_cfg.int_gpio_num = GPIO_NUM_NC;
  err = esp_lcd_touch_new_i2c_axs15231b(tio, &tp_cfg, &g_touch);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "touch_new_i2c_axs15231b: %s", esp_err_to_name(err));
    g_touch = nullptr;
    return false;
  }
  ESP_LOGI(kTag, "AXS15231B touch up (esp_lcd_touch on shared bus, sda=%d scl=%d)",
           sda, scl);
  return true;
}

bool lcd_axs15231b_touch_poll(int* x, int* y) {
  if (g_touch == nullptr) {
    return false;
  }
  if (esp_lcd_touch_read_data(g_touch) != ESP_OK) {
    return false;
  }
  uint16_t tx[1] = {0};
  uint16_t ty[1] = {0};
  uint8_t cnt = 0;
  const bool down =
      esp_lcd_touch_get_coordinates(g_touch, tx, ty, nullptr, &cnt, 1);
  if (!down || cnt == 0) {
    return false;
  }
  // TEMP bring-up telemetry: confirm coordinates + axis orientation.
  ESP_LOGI(kTag, "touch x=%d y=%d", tx[0], ty[0]);
  if (x != nullptr) {
    *x = tx[0];
  }
  if (y != nullptr) {
    *y = ty[0];
  }
  return true;
}

}  // namespace halesp

#endif  // CONFIG_NEON_BOARD_LINKSYNC_JC3248
