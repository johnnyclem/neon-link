// Local UI task: renders the portable menu model to a 128×128 panel
// (SSD1327 / SH1107 over I2C, optional SH1107 over SPI) and feeds it
// encoder input. Display flushes stay on core 0 — irrelevant to the
// pulse path on core 1.

#include "esp_log.h"
#include "esp_system.h"
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
  // Before the first sync the hero readout shows its placeholder rather
  // than a default tempo the module is not actually running at.
  s->tempo_valid = tl.tempo_mpb_q32 != 0;
  s->ble_on = neon_config().ble_enabled != 0;
  s->playing = tl.playing != 0;
  s->quantum_beats = tl.quantum_beats != 0 ? tl.quantum_beats : 4;

  s->phase_milli_beats = neon::phase_milli_beats(tl, esp_timer_get_time());

  const neon::ActiveNet net = netman::preference().active();
  s->active_net = net == neon::ActiveNet::kEthernet ? 1
                  : net == neon::ActiveNet::kWifi   ? 2
                                                    : 0;
  s->peers = app_status_peers();
  s->ext_clock = app_status_ext_clock();
  s->setup_ap = netman::ap_is_up();
  s->big_beat_display = neon_config().big_beat_display != 0;
  s->ip[0] = '\0';
  netman::primary_ip(s->ip, sizeof(s->ip));
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

  // Brightness is pushed to the controller only when it changes; a
  // contrast write per frame would waste I2C bandwidth for nothing.
  uint8_t applied_brightness = 0;
  bool brightness_applied = false;

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    const int detents = halesp::encoder_take_detents();
    if (detents != 0) {
      menu.on_rotate(detents);
    }
    switch (halesp::encoder_take_press()) {
      case halesp::EncoderPress::kShort:
        menu.on_click();
        break;
      case halesp::EncoderPress::kLong:
        menu.on_long_press();
        break;
      case halesp::EncoderPress::kNone:
        break;
    }
    if (menu.take_dirty()) {
      // Merge rather than write the whole struct back: the menu holds a
      // snapshot, and other tasks own fields it never touches (tempo, which
      // the Link service rewrites on every tap; WiFi and access point,
      // which the editor owns). Writing ui_cfg wholesale would revert them.
      neon::Config live = neon_config();
      live.engine = ui_cfg.engine;
      live.quantum_beats = ui_cfg.quantum_beats;
      live.clock_source = ui_cfg.clock_source;
      live.clock_in_ppqn = ui_cfg.clock_in_ppqn;
      live.midi_nudge_us = ui_cfg.midi_nudge_us;
      live.start_stop_sync = ui_cfg.start_stop_sync;
      live.display_brightness = ui_cfg.display_brightness;
      live.big_beat_display = ui_cfg.big_beat_display;
      neon_config_apply(live);
      ui_cfg = live;
    } else if (!menu.editing()) {
      // Not mid-edit: adopt whatever the editor or a preset recall wrote.
      ui_cfg = neon_config();
    }

    const uint8_t want_brightness = ui_cfg.display_brightness;
    if (have_display &&
        (!brightness_applied || want_brightness != applied_brightness)) {
      oledui::panel_set_brightness(want_brightness);
      applied_brightness = want_brightness;
      brightness_applied = true;
    }
    if (menu.take_action() == neon::MenuModel::Action::kReboot) {
      // Never restart with a debounced config write still only in RAM.
      neon_config_flush_now();
      esp_restart();
    }

    neon::UiStatus status;
    assemble_status(&status);

    halesp::status_led_net(status.active_net != 0);
    halesp::status_led_run(status.playing);
    halesp::status_led_beat((status.phase_milli_beats % 1000) < 150);

    if (have_display && want_brightness != 0) {
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
