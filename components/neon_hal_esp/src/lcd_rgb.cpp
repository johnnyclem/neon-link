#include "halesp/lcd_rgb.hpp"

#include "sdkconfig.h"

#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/i2c_bus.hpp"

namespace halesp {
namespace {

const char* kTag = "lcd_rgb";

constexpr uint8_t kStc8Addr = 0x2F;
constexpr uint8_t kStc8RegGpio = 0x18;
constexpr uint8_t kStc8RegPwm = 0x20;
constexpr uint8_t kStc8GpioBlPower = 3;
constexpr uint8_t kStc8PwmBl = 0;

esp_lcd_panel_handle_t g_panel = nullptr;
esp_lcd_touch_handle_t g_touch = nullptr;

bool stc8_write(uint8_t reg, uint8_t val) {
  const uint8_t pkt[2] = {reg, val};
  return i2c_write(kStc8Addr, pkt, sizeof(pkt), 100);
}

bool blight_on(uint8_t duty) {
  // Elecrow STC8 PWM is 0–100 percent, not 0–255. Values above 100
  // are ignored and the panel sits at the factory trickle.
  if (duty > 100) {
    duty = 100;
  }
  const bool pwr =
      stc8_write(static_cast<uint8_t>(kStc8RegGpio + kStc8GpioBlPower), 1);
  const bool pwm =
      stc8_write(static_cast<uint8_t>(kStc8RegPwm + kStc8PwmBl), duty);
  ESP_LOGI(kTag, "STC8 0x2F backlight pwr=%d pwm=%u ok=%d/%d",
           1, static_cast<unsigned>(duty), pwr ? 1 : 0, pwm ? 1 : 0);
  return pwr && pwm;
}

bool g_ldos = false;

bool acquire_ldos() {
  if (g_ldos) {
    return true;
  }
  // Factory firmware: LDO3 = 2.5 V analog, LDO4 = 3.3 V I/O. RGB will
  // not come up if these stay at the ROM defaults. The C6 module is
  // also on the 3.3 V rail — Hosted must not reset it before this.
  esp_ldo_channel_handle_t ldo3 = nullptr;
  esp_ldo_channel_handle_t ldo4 = nullptr;
  esp_ldo_channel_config_t c3 = {};
  c3.chan_id = 3;
  c3.voltage_mv = 2500;
  esp_ldo_channel_config_t c4 = {};
  c4.chan_id = 4;
  c4.voltage_mv = 3300;
  if (esp_ldo_acquire_channel(&c3, &ldo3) != ESP_OK) {
    ESP_LOGE(kTag, "LDO3 2.5 V acquire failed");
    return false;
  }
  if (esp_ldo_acquire_channel(&c4, &ldo4) != ESP_OK) {
    ESP_LOGE(kTag, "LDO4 3.3 V acquire failed");
    return false;
  }
  ESP_LOGI(kTag, "LDO3=2.5 V LDO4=3.3 V");
  g_ldos = true;
  return true;
}

}  // namespace

bool lcd_rgb_ldos() { return acquire_ldos(); }

bool lcd_rgb_init() {
  if (g_panel != nullptr) {
    return true;
  }
  if (!acquire_ldos()) {
    return false;
  }

  // Give Hosted's boot-time SDIO TX a moment before we grab ~1 MB of
  // PSRAM for the scanout buffer. Concurrent free of a failed Hosted
  // packet + RGB alloc double-freed tlsf on first boot.
  vTaskDelay(pdMS_TO_TICKS(200));

  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.data_width = 16;
  cfg.dma_burst_size = 64;
  cfg.num_fbs = 1;
  cfg.bounce_buffer_size_px = 20 * kLcdW;
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.disp_gpio_num = -1;
  cfg.pclk_gpio_num = 3;
  cfg.vsync_gpio_num = 41;
  cfg.hsync_gpio_num = 40;
  cfg.de_gpio_num = 2;
  cfg.data_gpio_nums[0] = 8;
  cfg.data_gpio_nums[1] = 7;
  cfg.data_gpio_nums[2] = 6;
  cfg.data_gpio_nums[3] = 5;
  cfg.data_gpio_nums[4] = 4;
  cfg.data_gpio_nums[5] = 14;
  cfg.data_gpio_nums[6] = 13;
  cfg.data_gpio_nums[7] = 12;
  cfg.data_gpio_nums[8] = 11;
  cfg.data_gpio_nums[9] = 10;
  cfg.data_gpio_nums[10] = 9;
  cfg.data_gpio_nums[11] = 19;
  cfg.data_gpio_nums[12] = 18;
  cfg.data_gpio_nums[13] = 17;
  cfg.data_gpio_nums[14] = 16;
  cfg.data_gpio_nums[15] = 15;
  cfg.timings.pclk_hz = 25 * 1000 * 1000;
  cfg.timings.h_res = kLcdW;
  cfg.timings.v_res = kLcdH;
  cfg.timings.hsync_pulse_width = 4;
  cfg.timings.hsync_back_porch = 8;
  cfg.timings.hsync_front_porch = 8;
  cfg.timings.vsync_pulse_width = 4;
  cfg.timings.vsync_back_porch = 16;
  cfg.timings.vsync_front_porch = 16;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.timings.flags.pclk_idle_high = 1;
  cfg.flags.fb_in_psram = 1;

  if (esp_lcd_new_rgb_panel(&cfg, &g_panel) != ESP_OK) {
    ESP_LOGE(kTag, "esp_lcd_new_rgb_panel failed");
    g_panel = nullptr;
    return false;
  }
  if (esp_lcd_panel_reset(g_panel) != ESP_OK ||
      esp_lcd_panel_init(g_panel) != ESP_OK) {
    ESP_LOGE(kTag, "panel reset/init failed");
    return false;
  }
  (void)esp_lcd_panel_disp_on_off(g_panel, true);
  lcd_rgb_backlight(100);  // Elecrow PWM is 0–100 percent
  ESP_LOGI(kTag, "RGB 800x480 up");
  return true;
}

void lcd_rgb_backlight(uint8_t duty) {
  if (!blight_on(duty)) {
    ESP_LOGW(kTag, "STC8 backlight write failed (I2C 0x2F)");
  }
}

bool lcd_rgb_blit(const uint16_t* rgb565, int x, int y, int w, int h) {
  if (g_panel == nullptr || rgb565 == nullptr) {
    return false;
  }
  return esp_lcd_panel_draw_bitmap(g_panel, x, y, x + w, y + h, rgb565) ==
         ESP_OK;
}

bool lcd_touch_init() {
  if (g_touch != nullptr) {
    return true;
  }
  if (i2c_bus() == nullptr) {
    ESP_LOGW(kTag, "touch: no I2C bus");
    return false;
  }

  // INT low while RST rises → I2C 0x5D (Elecrow Advance 5.0"). The
  // driver then owns neither pin so a second address try cannot double-
  // claim GPIO 36 (HSYNC is 40; RST is 36).
  gpio_config_t rst_int = {};
  rst_int.mode = GPIO_MODE_OUTPUT;
  rst_int.pin_bit_mask = (1ull << static_cast<unsigned>(kPinTouchRst)) |
                         (1ull << static_cast<unsigned>(kPinTouchInt));
  gpio_config(&rst_int);
  gpio_set_level(static_cast<gpio_num_t>(kPinTouchInt), 0);
  gpio_set_level(static_cast<gpio_num_t>(kPinTouchRst), 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(static_cast<gpio_num_t>(kPinTouchRst), 1);
  vTaskDelay(pdMS_TO_TICKS(60));
  gpio_config_t int_in = {};
  int_in.mode = GPIO_MODE_INPUT;
  int_in.pin_bit_mask = 1ull << static_cast<unsigned>(kPinTouchInt);
  gpio_config(&int_in);

  constexpr uint8_t kAddrGt911 = 0x5d;
  constexpr uint8_t kAddrGt911Alt = 0x14;
  const bool ack_5d = i2c_probe(kAddrGt911, 80);
  const bool ack_14 = i2c_probe(kAddrGt911Alt, 80);
  ESP_LOGI(kTag, "GT911 probe 0x5D=%s 0x14=%s rst=%d int=%d",
           ack_5d ? "ACK" : "nack", ack_14 ? "ACK" : "nack", kPinTouchRst,
           kPinTouchInt);

  esp_lcd_touch_config_t tp_cfg = {};
  tp_cfg.x_max = static_cast<uint16_t>(kLcdW);
  tp_cfg.y_max = static_cast<uint16_t>(kLcdH);
  tp_cfg.rst_gpio_num = GPIO_NUM_NC;
  tp_cfg.int_gpio_num = GPIO_NUM_NC;
  tp_cfg.levels.reset = 0;
  tp_cfg.levels.interrupt = 0;
  tp_cfg.flags.swap_xy = 0;
  tp_cfg.flags.mirror_x = 0;
  tp_cfg.flags.mirror_y = 0;

  auto make_touch_io = [](uint32_t addr) {
    esp_lcd_panel_io_i2c_config_t io_cfg = {};
    io_cfg.dev_addr = addr;
    io_cfg.control_phase_bytes = 1;
    io_cfg.lcd_cmd_bits = 16;
    io_cfg.flags.disable_control_phase = 1;
    io_cfg.scl_speed_hz = 100000;
    return io_cfg;
  };

  const uint32_t addrs[2] = {
      ack_14 && !ack_5d
          ? static_cast<uint32_t>(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP)
          : static_cast<uint32_t>(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS),
      ack_14 && !ack_5d
          ? static_cast<uint32_t>(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS)
          : static_cast<uint32_t>(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP),
  };

  esp_err_t err = ESP_FAIL;
  for (uint32_t addr : addrs) {
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_i2c_config_t io_cfg = make_touch_io(addr);
    err = esp_lcd_new_panel_io_i2c(i2c_bus(), &io_cfg, &io);
    if (err != ESP_OK) {
      ESP_LOGW(kTag, "touch io @0x%02x %s", static_cast<unsigned>(addr),
               esp_err_to_name(err));
      continue;
    }
    err = esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, &g_touch);
    ESP_LOGI(kTag, "touch GT911 @0x%02x %s", static_cast<unsigned>(addr),
             err == ESP_OK ? "ok" : esp_err_to_name(err));
    if (err == ESP_OK && g_touch != nullptr) {
      return true;
    }
    g_touch = nullptr;
  }
  ESP_LOGW(kTag, "touch: GT911 init failed");
  return false;
}

bool lcd_touch_ok() { return g_touch != nullptr; }

bool lcd_touch_poll(int* x, int* y) {
  if (g_touch == nullptr) {
    return false;
  }
  if (esp_lcd_touch_read_data(g_touch) != ESP_OK) {
    return false;
  }
  esp_lcd_touch_point_data_t pts[1] = {};
  uint8_t n = 0;
  if (esp_lcd_touch_get_data(g_touch, pts, &n, 1) != ESP_OK || n == 0) {
    return false;
  }
  if (x != nullptr) {
    *x = static_cast<int>(pts[0].x);
  }
  if (y != nullptr) {
    *y = static_cast<int>(pts[0].y);
  }
  return true;
}

}  // namespace halesp

#elif !CONFIG_NEON_BOARD_LINKSYNC_TAB5

namespace halesp {

bool lcd_rgb_ldos() { return false; }
bool lcd_rgb_init() { return false; }
void lcd_rgb_backlight(uint8_t) {}
bool lcd_rgb_blit(const uint16_t*, int, int, int, int) { return false; }
bool lcd_touch_init() { return false; }
bool lcd_touch_ok() { return false; }
bool lcd_touch_poll(int*, int*) { return false; }

}  // namespace halesp

#endif
