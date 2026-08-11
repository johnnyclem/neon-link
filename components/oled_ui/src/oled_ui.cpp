// Local UI task: renders the portable menu model to a 128×128 panel
// (SSD1327 / SH1107 over I2C, optional SH1107 over SPI) and feeds it
// encoder input. Display flushes stay on core 0 — irrelevant to the
// pulse path on core 1.

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
#include "oledui/panel128.hpp"

namespace {

const char* kTag = "oled_ui";
// ~10 fps: a full SSD1327 grayscale frame is 8 KB over I2C (~200 ms worst
// case at 400 kHz). Diff-less full flushes are fine; lower rate keeps the
// bus free for GP8413 / ADS1015.
constexpr TickType_t kFrameTicks = pdMS_TO_TICKS(100);

void assemble_status(neon::UiStatus* s) {
  neon::TimelineSnapshot tl;
  timeline_bus().read(tl);
  const uint64_t mpb_us = (tl.tempo_mpb_q32 + (1ull << 31)) >> 32;
  s->milli_bpm =
      mpb_us != 0 ? static_cast<uint32_t>(60000000000ull / mpb_us) : 120000;
  s->playing = tl.playing != 0;
  s->quantum_beats = tl.quantum_beats != 0 ? tl.quantum_beats : 4;

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
  const oledui::PanelKind kind = oledui::panel_init();
  const bool have_display = kind != oledui::PanelKind::kNone;
  if (!have_display) {
    ESP_LOGW(kTag, "no OLED detected; UI task drives LEDs only");
  } else {
    ESP_LOGI(kTag, "panel kind=%d", static_cast<int>(kind));
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
      neon_config_apply(ui_cfg);
    }

    neon::UiStatus status;
    assemble_status(&status);

    halesp::status_led_net(status.active_net != 0);
    halesp::status_led_run(status.playing);
    halesp::status_led_beat((status.phase_milli_beats % 1000) < 150);

    if (have_display) {
      neon::render_ui(menu, status, fb);
      if (!oledui::panel_flush(fb)) {
        ESP_LOGW(kTag, "panel flush failed");
      }
    }
    vTaskDelayUntil(&wake, kFrameTicks);
  }
}

}  // namespace

void oledui_start() {
  // 128×128 FB (2 KB) + SSD1327 pack buffer lives in panel128; give the
  // UI task room for both.
  xTaskCreatePinnedToCore(ui_task, "oled_ui", 8192, nullptr, 3, nullptr, 0);
}
