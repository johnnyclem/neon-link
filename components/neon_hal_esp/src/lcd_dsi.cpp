#include "halesp/lcd_rgb.hpp"

#include "sdkconfig.h"

#if CONFIG_NEON_BOARD_LINKSYNC_TAB5

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7123.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch_st7123.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/i2c_bus.hpp"

#include "tab5/disp_init_ili9881c.h"
#include "tab5/disp_init_st7123.h"

namespace halesp {
namespace {

const char* kTag = "lcd_dsi";

constexpr uint8_t kPi4Lcd = 0x43;   // PI4IOE5V6408 ADDR low
constexpr uint8_t kPi4Wifi = 0x44;  // PI4IOE5V6408 ADDR high
constexpr uint8_t kPi4RegDir = 0x03;
constexpr uint8_t kPi4RegOut = 0x05;
constexpr uint8_t kPi4RegHiz = 0x07;
constexpr uint8_t kLcdEnBit = 1u << 4;    // expander 0x43 P4
constexpr uint8_t kTouchEnBit = 1u << 5;  // expander 0x43 P5
constexpr uint8_t kWifiEnBit = 1u << 0;   // expander 0x44 P0
constexpr int kBlGpio = 22;
constexpr int kTouchIntGpio = 23;
constexpr ledc_channel_t kBlChan = LEDC_CHANNEL_1;
constexpr uint8_t kAddrSt7123 = 0x55;
constexpr uint8_t kAddrGt911 = 0x14;
constexpr uint8_t kAddrGt911Alt = 0x5d;

esp_lcd_panel_handle_t g_panel = nullptr;
esp_lcd_touch_handle_t g_touch = nullptr;
bool g_ldos = false;
bool g_bl = false;

bool pi4_rmw(uint8_t addr, uint8_t reg, uint8_t set_bits, uint8_t clear_bits) {
  uint8_t v = 0;
  if (!i2c_write_read(addr, &reg, 1, &v, 1, 50)) {
    return false;
  }
  v = static_cast<uint8_t>((v | set_bits) & static_cast<uint8_t>(~clear_bits));
  const uint8_t pkt[2] = {reg, v};
  return i2c_write(addr, pkt, sizeof(pkt), 50);
}

// 1 = output, 0 = input. Drive high, push-pull (Hi-Z bit clear).
bool pi4_out_high(uint8_t addr, uint8_t bit) {
  if (!pi4_rmw(addr, kPi4RegDir, bit, 0)) {
    return false;
  }
  if (!pi4_rmw(addr, kPi4RegOut, bit, 0)) {
    return false;
  }
  return pi4_rmw(addr, kPi4RegHiz, 0, bit);
}

bool blight_pwm(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  if (!g_bl) {
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num = LEDC_TIMER_0;
    t.freq_hz = 5000;
    t.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&t) != ESP_OK) {
      return false;
    }
    ledc_channel_config_t ch = {};
    ch.gpio_num = kBlGpio;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = kBlChan;
    ch.timer_sel = LEDC_TIMER_0;
    ch.duty = 0;
    if (ledc_channel_config(&ch) != ESP_OK) {
      return false;
    }
    g_bl = true;
  }
  const uint32_t duty = (1023u * percent) / 100u;
  if (ledc_set_duty(LEDC_LOW_SPEED_MODE, kBlChan, duty) != ESP_OK ||
      ledc_update_duty(LEDC_LOW_SPEED_MODE, kBlChan) != ESP_OK) {
    return false;
  }
  ESP_LOGI(kTag, "backlight %u%%", static_cast<unsigned>(percent));
  return true;
}

}  // namespace

bool lcd_rgb_ldos() {
  if (g_ldos) {
    return true;
  }
  if (kPinI2cSda >= 0 && !i2c_bus_init(kPinI2cSda, kPinI2cScl)) {
    ESP_LOGE(kTag, "I2C for PI4IOE failed");
    return false;
  }
  const bool lcd_en = pi4_out_high(kPi4Lcd, kLcdEnBit);
  const bool touch_en = pi4_out_high(kPi4Lcd, kTouchEnBit);
  const bool wifi_en = pi4_out_high(kPi4Wifi, kWifiEnBit);
  ESP_LOGI(kTag, "PI4IOE LCD_EN=%d TOUCH_EN=%d WIFI_EN=%d", lcd_en ? 1 : 0,
           touch_en ? 1 : 0, wifi_en ? 1 : 0);

  esp_ldo_channel_handle_t ldo3 = nullptr;
  esp_ldo_channel_config_t c3 = {};
  c3.chan_id = 3;
  c3.voltage_mv = 2500;
  if (esp_ldo_acquire_channel(&c3, &ldo3) != ESP_OK) {
    ESP_LOGE(kTag, "LDO3 2.5 V (DPHY) acquire failed");
    return false;
  }
  ESP_LOGI(kTag, "LDO3=2.5 V (MIPI DPHY)");
  g_ldos = true;
  return true;
}

bool lcd_rgb_init() {
  if (g_panel != nullptr) {
    return true;
  }
  if (!lcd_rgb_ldos()) {
    return false;
  }
  (void)blight_pwm(0);
  vTaskDelay(pdMS_TO_TICKS(80));

  const bool st7123 = i2c_probe(kAddrSt7123, 80);
  const bool gt911 =
      i2c_probe(kAddrGt911, 80) || i2c_probe(kAddrGt911Alt, 80);
  ESP_LOGI(kTag, "panel probe ST7123@0x55=%s GT911=%s",
           st7123 ? "ACK" : "nack", gt911 ? "ACK" : "nack");
  const bool use_st7123 = st7123;

  esp_lcd_dsi_bus_handle_t bus = nullptr;
  esp_lcd_dsi_bus_config_t bus_cfg = {};
  bus_cfg.bus_id = 0;
  bus_cfg.num_data_lanes = 2;
  bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
  bus_cfg.lane_bit_rate_mbps = 1000;
  if (esp_lcd_new_dsi_bus(&bus_cfg, &bus) != ESP_OK) {
    ESP_LOGE(kTag, "esp_lcd_new_dsi_bus failed");
    return false;
  }
  esp_lcd_dbi_io_config_t dbi = {};
  dbi.virtual_channel = 0;
  dbi.lcd_cmd_bits = 8;
  dbi.lcd_param_bits = 8;
  esp_lcd_panel_io_handle_t io = nullptr;
  if (esp_lcd_new_panel_io_dbi(bus, &dbi, &io) != ESP_OK) {
    ESP_LOGE(kTag, "esp_lcd_new_panel_io_dbi failed");
    return false;
  }

  const esp_lcd_dpi_panel_config_t dpi_ili = {
      .virtual_channel = 0,
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = 60,
      .in_color_format = LCD_COLOR_FMT_RGB565,
      .num_fbs = 1,
      .video_timing =
          {
              .h_size = static_cast<uint32_t>(kLcdW),
              .v_size = static_cast<uint32_t>(kLcdH),
              .hsync_pulse_width = 40,
              .hsync_back_porch = 140,
              .hsync_front_porch = 40,
              .vsync_pulse_width = 4,
              .vsync_back_porch = 20,
              .vsync_front_porch = 20,
          },
      .flags = {.use_dma2d = 1},
  };
  const esp_lcd_dpi_panel_config_t dpi_st = {
      .virtual_channel = 0,
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = 70,
      .in_color_format = LCD_COLOR_FMT_RGB565,
      .num_fbs = 1,
      .video_timing =
          {
              .h_size = static_cast<uint32_t>(kLcdW),
              .v_size = static_cast<uint32_t>(kLcdH),
              .hsync_pulse_width = 2,
              .hsync_back_porch = 40,
              .hsync_front_porch = 40,
              .vsync_pulse_width = 2,
              .vsync_back_porch = 8,
              .vsync_front_porch = 220,
          },
      .flags = {.use_dma2d = 1},
  };

  ili9881c_vendor_config_t vend_ili = {};
  vend_ili.init_cmds = disp_init_data_ili9881c;
  vend_ili.init_cmds_size = static_cast<uint16_t>(
      sizeof(disp_init_data_ili9881c) / sizeof(disp_init_data_ili9881c[0]));
  vend_ili.mipi_config.dsi_bus = bus;
  vend_ili.mipi_config.dpi_config = &dpi_ili;
  vend_ili.mipi_config.lane_num = 2;

  st7123_vendor_config_t vend_st = {};
  vend_st.init_cmds = disp_init_data_st7123;
  vend_st.init_cmds_size = static_cast<uint16_t>(
      sizeof(disp_init_data_st7123) / sizeof(disp_init_data_st7123[0]));
  vend_st.mipi_config.dsi_bus = bus;
  vend_st.mipi_config.dpi_config = &dpi_st;

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = -1;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.bits_per_pixel = 16;
  panel_cfg.vendor_config = use_st7123 ? static_cast<void*>(&vend_st)
                                       : static_cast<void*>(&vend_ili);

  esp_err_t err = ESP_FAIL;
  if (use_st7123) {
    err = esp_lcd_new_panel_st7123(io, &panel_cfg, &g_panel);
  } else {
    err = esp_lcd_new_panel_ili9881c(io, &panel_cfg, &g_panel);
  }
  if (err != ESP_OK || g_panel == nullptr) {
    ESP_LOGE(kTag, "new panel failed (%s) st7123=%d", esp_err_to_name(err),
             use_st7123 ? 1 : 0);
    g_panel = nullptr;
    return false;
  }
  if (esp_lcd_panel_reset(g_panel) != ESP_OK ||
      esp_lcd_panel_init(g_panel) != ESP_OK) {
    ESP_LOGE(kTag, "panel reset/init failed");
    return false;
  }
  (void)esp_lcd_panel_invert_color(g_panel, false);
  (void)esp_lcd_panel_mirror(g_panel, false, false);
  (void)esp_lcd_panel_disp_on_off(g_panel, true);
  lcd_rgb_backlight(80);
  ESP_LOGI(kTag, "MIPI-DSI %dx%d up (%s)", kLcdW, kLcdH,
           use_st7123 ? "ST7123" : "ILI9881C");
  return true;
}

void lcd_rgb_backlight(uint8_t duty) { (void)blight_pwm(duty); }

bool lcd_rgb_blit(const uint16_t* rgb565, int x, int y, int w, int h) {
  if (g_panel == nullptr || rgb565 == nullptr) {
    return false;
  }
  return esp_lcd_panel_draw_bitmap(g_panel, x, y, x + w, y + h, rgb565) ==
         ESP_OK;
}

// No double-buffered scanout on the DSI path yet; lcd_service falls back
// to composing in its own buffer and blitting.
uint16_t* lcd_rgb_next_frame() { return nullptr; }
bool lcd_rgb_present() { return false; }

bool lcd_touch_init() {
  if (g_touch != nullptr) {
    return true;
  }
  if (i2c_bus() == nullptr) {
    ESP_LOGW(kTag, "touch: no I2C bus");
    return false;
  }
  (void)gpio_install_isr_service(0);

  esp_lcd_touch_config_t tp_cfg = {};
  tp_cfg.x_max = static_cast<uint16_t>(kLcdW);
  tp_cfg.y_max = static_cast<uint16_t>(kLcdH);
  tp_cfg.rst_gpio_num = GPIO_NUM_NC;
  tp_cfg.int_gpio_num = static_cast<gpio_num_t>(kTouchIntGpio);
  tp_cfg.levels.reset = 0;
  tp_cfg.levels.interrupt = 0;
  tp_cfg.flags.swap_xy = 0;
  tp_cfg.flags.mirror_x = 0;
  tp_cfg.flags.mirror_y = 0;

  const bool st7123 = i2c_probe(kAddrSt7123, 80);
  const bool gt911 =
      i2c_probe(kAddrGt911, 80) || i2c_probe(kAddrGt911Alt, 80);

  auto make_touch_io = [](uint32_t addr) {
    esp_lcd_panel_io_i2c_config_t io_cfg = {};
    io_cfg.dev_addr = addr;
    io_cfg.control_phase_bytes = 1;
    io_cfg.lcd_cmd_bits = 16;
    io_cfg.flags.disable_control_phase = 1;
    io_cfg.scl_speed_hz = 100000;
    return io_cfg;
  };

  esp_lcd_panel_io_handle_t io = nullptr;
  esp_err_t err = ESP_FAIL;
  if (st7123) {
    esp_lcd_panel_io_i2c_config_t io_cfg =
        make_touch_io(ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS);
    err = esp_lcd_new_panel_io_i2c(i2c_bus(), &io_cfg, &io);
    if (err == ESP_OK) {
      err = esp_lcd_touch_new_i2c_st7123(io, &tp_cfg, &g_touch);
    }
    ESP_LOGI(kTag, "touch ST7123 @0x55 %s",
             err == ESP_OK ? "ok" : esp_err_to_name(err));
  } else if (gt911) {
    // Tab5 v1: INT is pulled up and blocks GT911 unless held low.
    gpio_config_t int_cfg = {};
    int_cfg.mode = GPIO_MODE_OUTPUT;
    int_cfg.pin_bit_mask = 1ull << static_cast<unsigned>(kTouchIntGpio);
    int_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&int_cfg);
    gpio_set_level(static_cast<gpio_num_t>(kTouchIntGpio), 0);
    tp_cfg.int_gpio_num = GPIO_NUM_NC;

    const uint32_t addr = i2c_probe(kAddrGt911Alt, 50)
                              ? ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP
                              : ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    esp_lcd_panel_io_i2c_config_t io_cfg = make_touch_io(addr);
    err = esp_lcd_new_panel_io_i2c(i2c_bus(), &io_cfg, &io);
    if (err == ESP_OK) {
      err = esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, &g_touch);
    }
    ESP_LOGI(kTag, "touch GT911 %s",
             err == ESP_OK ? "ok" : esp_err_to_name(err));
  } else {
    ESP_LOGW(kTag, "touch: no ST7123/GT911 on I2C");
    return false;
  }
  if (err != ESP_OK || g_touch == nullptr) {
    g_touch = nullptr;
    return false;
  }
  return true;
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

#endif  // CONFIG_NEON_BOARD_LINKSYNC_TAB5
