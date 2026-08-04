// Local UI task: renders the portable menu model to the SSD1306 and feeds
// it encoder input. Display flushes are full-frame (1 KB over I2C at
// 400 kHz ≈ 25 ms worst case — fine at ~15 fps, and irrelevant to the
// pulse path on core 1).

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "board_pins.h"
#include "halesp/encoder_pcnt.hpp"
#include "halesp/status_leds.hpp"
#include "neon/gfx/framebuffer.hpp"
#include "neon/net/preference.hpp"
#include "neon/ui/menu_model.hpp"
#include "neon/ui/render.hpp"
#include "netman/net_manager.h"
#include "oledui/oled_ui.h"

namespace {

const char* kTag = "oled_ui";
constexpr TickType_t kFrameTicks = pdMS_TO_TICKS(66);  // ~15 fps

esp_lcd_panel_handle_t g_panel = nullptr;

bool display_init() {
  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = I2C_NUM_0;
  bus_cfg.sda_io_num = static_cast<gpio_num_t>(kPinI2cSda);
  bus_cfg.scl_io_num = static_cast<gpio_num_t>(kPinI2cScl);
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
    return false;
  }

  esp_lcd_panel_io_i2c_config_t io_cfg = {};
  io_cfg.dev_addr = 0x3c;
  io_cfg.scl_speed_hz = 400000;
  io_cfg.control_phase_bytes = 1;
  io_cfg.lcd_cmd_bits = 8;
  io_cfg.lcd_param_bits = 8;
  io_cfg.dc_bit_offset = 6;
  esp_lcd_panel_io_handle_t io = nullptr;
  if (esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io) != ESP_OK) {
    return false;
  }

  esp_lcd_panel_ssd1306_config_t ssd_cfg = {};
  ssd_cfg.height = 64;
  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.bits_per_pixel = 1;
  panel_cfg.reset_gpio_num = -1;
  panel_cfg.vendor_config = &ssd_cfg;
  if (esp_lcd_new_panel_ssd1306(io, &panel_cfg, &g_panel) != ESP_OK) {
    return false;
  }
  if (esp_lcd_panel_reset(g_panel) != ESP_OK ||
      esp_lcd_panel_init(g_panel) != ESP_OK ||
      esp_lcd_panel_disp_on_off(g_panel, true) != ESP_OK) {
    return false;
  }
  return true;
}

void assemble_status(neon::UiStatus* s) {
  neon::TimelineSnapshot tl;
  timeline_bus().read(tl);
  const uint64_t mpb_us = (tl.tempo_mpb_q32 + (1ull << 31)) >> 32;
  s->milli_bpm =
      mpb_us != 0 ? static_cast<uint32_t>(60000000000ull / mpb_us) : 120000;
  s->playing = tl.playing != 0;
  s->quantum_beats = tl.quantum_beats != 0 ? tl.quantum_beats : 4;

  // Phase within the bar, milli-beats.
  const int64_t now = esp_timer_get_time();
  if (tl.tempo_mpb_q32 != 0) {
    const double mpb_us =
        static_cast<double>(tl.tempo_mpb_q32) / 4294967296.0;
    double beat = static_cast<double>(tl.beat_at_origin_q32) / 4294967296.0 +
                  static_cast<double>(now - tl.origin_us) / mpb_us;
    const double q = static_cast<double>(s->quantum_beats);
    double bar_pos = beat - static_cast<int64_t>(beat / q) * q;
    if (bar_pos < 0) {
      bar_pos += q;
    }
    s->phase_milli_beats = static_cast<uint32_t>(bar_pos * 1000.0);
  }

  const neon::ActiveNet net = netman::preference().active();
  s->active_net = net == neon::ActiveNet::kEthernet ? 1
                  : net == neon::ActiveNet::kWifi   ? 2
                                                    : 0;
  s->peers = app_status_peers();
  s->ext_clock = app_status_ext_clock();
}

void ui_task(void*) {
  const bool have_display = display_init();
  if (!have_display) {
    ESP_LOGW(kTag, "no OLED detected; UI task drives LEDs only");
  }
  halesp::encoder_init(kPinEncA, kPinEncB, kPinEncSw);
  halesp::status_leds_init(kPinLedNet, kPinLedBeat, kPinLedRun);

  neon::Config ui_cfg = neon_config();
  neon::MenuModel menu(&ui_cfg);
  neon::Framebuffer fb;

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    const int detents = halesp::encoder_take_detents();
    if (detents != 0) {
      menu.on_rotate(detents);
    }
    if (halesp::encoder_clicked()) {
      menu.on_click();
    }
    if (menu.take_dirty()) {
      neon_config_apply(ui_cfg);  // live-apply + debounced persist
    }

    neon::UiStatus status;
    assemble_status(&status);

    halesp::status_led_net(status.active_net != 0);
    halesp::status_led_run(status.playing);
    // Beat LED: first 15% of every beat.
    halesp::status_led_beat((status.phase_milli_beats % 1000) < 150);

    if (have_display) {
      neon::render_ui(menu, status, fb);
      esp_lcd_panel_draw_bitmap(g_panel, 0, 0, neon::Framebuffer::kWidth,
                                neon::Framebuffer::kHeight, fb.data());
    }
    vTaskDelayUntil(&wake, kFrameTicks);
  }
}

}  // namespace

void oledui_start() {
  xTaskCreatePinnedToCore(ui_task, "oled_ui", 6144, nullptr, 3, nullptr, 0);
}
