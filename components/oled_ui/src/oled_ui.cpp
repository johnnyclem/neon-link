// Local UI task: renders the portable menu model to a 128×128 panel
// (SSD1327 / SH1107 over I2C, optional SH1107 over SPI) and feeds it
// encoder input. Display flushes stay on core 0 — irrelevant to the
// pulse path on core 1.

#include <cstdio>
#include <cstdlib>

#include "esp_app_desc.h"
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
#include "neon/transport.hpp"
#include "neon/ui/icons_gen.hpp"
#include "neon/ui/menu_model.hpp"
#include "neon/ui/idle_dimmer.hpp"
#include "neon/ui/render.hpp"
#include "neon/ui/widgets.hpp"
#include "netman/net_manager.h"
#include "oledui/oled_ui.h"
#include "oledui/panel128.hpp"

namespace {

const char* kTag = "oled_ui";
// ~10 fps: a full SSD1327 grayscale frame is 8 KB over I2C (~200 ms worst
// case at 400 kHz). Diff-less full flushes are fine; lower rate keeps the
// bus free for GP8413 / ADS1015.
constexpr TickType_t kFrameTicks = pdMS_TO_TICKS(100);
constexpr int64_t kBootIgnoreUs = 3000000;
constexpr int64_t kDoubleClickUs = 500000;
constexpr int64_t kAfterOpenIgnoreUs = 200000;

int64_t g_ui_boot_us = 0;
int64_t g_pending_home_short_us = 0;
int64_t g_ignore_press_until_us = 0;

void toggle_transport() {
  neon::TimelineSnapshot tl{};
  timeline_bus().read(tl);
  ControlCommand cmd{};
  cmd.kind = tl.playing ? ControlCommand::Kind::kStopNow
                        : ControlCommand::Kind::kPlayNow;
  control_queue_push(cmd);
  ESP_LOGI(kTag, "encoder %s", tl.playing ? "stop" : "play");
}

bool boot_locked() {
  return g_ui_boot_us != 0 &&
         esp_timer_get_time() - g_ui_boot_us < kBootIgnoreUs;
}

void commit_pending_home_short(neon::MenuModel& menu) {
  if (g_pending_home_short_us == 0) {
    return;
  }
  if (esp_timer_get_time() - g_pending_home_short_us < kDoubleClickUs) {
    return;
  }
  g_pending_home_short_us = 0;
  if (menu.screen() == neon::MenuModel::Screen::kHome) {
    toggle_transport();
  }
}

void open_settings(neon::MenuModel& menu) {
  g_pending_home_short_us = 0;
  halesp::encoder_clear_press();
  if (menu.screen() == neon::MenuModel::Screen::kHome) {
    menu.on_click();
    // LIVE is row 0 and means "go home". A leftover edge from the
    // double-click must not land there. OUTPUTS is the first real page.
    menu.set_cursor(1);
    g_ignore_press_until_us = esp_timer_get_time() + kAfterOpenIgnoreUs;
  }
}

void handle_press(neon::MenuModel& menu, halesp::EncoderPress ev) {
  using S = neon::MenuModel::Screen;
  if (ev == halesp::EncoderPress::kNone) {
    return;
  }
  if (boot_locked()) {
    return;
  }
  if (esp_timer_get_time() < g_ignore_press_until_us) {
    return;
  }
  const bool home = menu.screen() == S::kHome;
  switch (ev) {
    case halesp::EncoderPress::kDouble:
    case halesp::EncoderPress::kShort:
    case halesp::EncoderPress::kLong:
      g_pending_home_short_us =
          ev == halesp::EncoderPress::kLong ? 0 : g_pending_home_short_us;
      if (home) {
        if (ev == halesp::EncoderPress::kLong) {
          break;
        }
        if (ev == halesp::EncoderPress::kDouble ||
            g_pending_home_short_us != 0) {
          open_settings(menu);
        } else {
          g_pending_home_short_us = esp_timer_get_time();
        }
      } else {
        // Settings and every submenu: any click selects. BACK/LIVE go home.
        g_pending_home_short_us = 0;
        ESP_LOGI(kTag, "menu click screen=%d cursor=%d",
                 static_cast<int>(menu.screen()), menu.cursor());
        menu.on_click();
      }
      break;
    case halesp::EncoderPress::kNone:
      break;
  }
}

void assemble_status(neon::UiStatus* s, int64_t now_us, int64_t phase_us) {
  neon::TimelineSnapshot tl;
  timeline_bus().read(tl);
  const uint64_t mpb_us = (tl.tempo_mpb_q32 + (1ull << 31)) >> 32;
  s->milli_bpm =
      mpb_us != 0 ? neon::milli_bpm_from_mpb_us(mpb_us) : 120000;
  // Before the first sync the hero readout shows its placeholder rather
  // than a default tempo the module is not actually running at.
  s->tempo_valid = tl.tempo_mpb_q32 != 0;
  s->ble_on = neon_config().ble_enabled != 0;
  s->playing = tl.playing != 0;
  s->quantum_beats = tl.quantum_beats != 0 ? tl.quantum_beats : 4;

  s->phase_milli_beats = neon::phase_milli_beats(tl, phase_us);
  // The fixed-rate icon clock, derived from the timebase rather than counted
  // per frame so it stays honest if the UI task ever misses a tick.
  s->anim_tick = static_cast<uint32_t>(
      now_us / (1000000 / neon::ui::kIconTickHz));

  const neon::ActiveNet net = netman::preference().active();
  s->active_net = net == neon::ActiveNet::kEthernet ? 1
                  : net == neon::ActiveNet::kWifi   ? 2
                                                    : 0;
  s->peers = app_status_peers();
  s->ext_clock = app_status_ext_clock();
  s->setup_ap = netman::ap_is_up();
  s->big_beat_display = neon_config().big_beat_display != 0;
  s->beat_style = static_cast<uint8_t>(neon_config().beat_style);
  s->ip[0] = '\0';
  netman::primary_ip(s->ip, sizeof(s->ip));

  const esp_app_desc_t* desc = esp_app_get_description();
  std::snprintf(s->firmware, sizeof(s->firmware), "%s",
               desc != nullptr ? desc->version : "unknown");

  const neon::Config& cfg = neon_config();
  std::snprintf(s->ap_pass, sizeof(s->ap_pass), "%s", cfg.ap_pass);
  std::snprintf(s->device_token, sizeof(s->device_token), "%s",
               cfg.device_token);
}

// Expander E0 (10k pot): absolute tempo, soft-pickup so a parked slider
// does not yank BPM at boot. Span matches the Tempo CV jack (config
// tempo_cv_min/max, default 20–300). Whole-BPM steps only.
void poll_pot_tempo() {
  const int adc = halesp::encoder_pot();
  if (adc < 0) {
    return;
  }
  static int boot_adc = -1;
  static bool engaged = false;
  static uint32_t last_mbpm = 0;
  if (boot_adc < 0) {
    boot_adc = adc;
    return;
  }
  constexpr int kPickup = 21;  // ~2% of 10-bit full scale
  if (!engaged) {
    if (std::abs(adc - boot_adc) < kPickup) {
      return;
    }
    engaged = true;
    ESP_LOGI(kTag, "pot tempo engaged adc=%d", adc);
  }
  const neon::Config& cfg = neon_config();
  uint16_t min_b = cfg.tempo_cv_min_bpm;
  uint16_t max_b = cfg.tempo_cv_max_bpm;
  if (max_b <= min_b) {
    min_b = 20;
    max_b = 300;
  }
  const uint32_t span = static_cast<uint32_t>(max_b - min_b);
  const uint32_t bpm =
      static_cast<uint32_t>(min_b) +
      (static_cast<uint32_t>(adc) * span + 511u) / 1023u;
  const uint32_t mbpm = bpm * 1000u;
  if (mbpm == last_mbpm) {
    return;
  }
  last_mbpm = mbpm;
  control_queue_push(
      {ControlCommand::Kind::kSetTempo, static_cast<int32_t>(mbpm)});
}

// Sleep until `target_us`. Bulk of the wait is a FreeRTOS delay so WiFi
// still runs; the last couple of milliseconds spin so a 1 ms tick cannot
// overshoot the display lead.
void wait_until_us(int64_t target_us) {
  for (;;) {
    const int64_t now = esp_timer_get_time();
    const int64_t remain = target_us - now;
    if (remain <= 0) {
      return;
    }
    if (remain > 2000) {
      uint32_t ms = static_cast<uint32_t>((remain - 1500) / 1000);
      if (ms < 1) {
        ms = 1;
      }
      vTaskDelay(pdMS_TO_TICKS(ms));
    }
  }
}

void ui_task(void*) {
  const oledui::PanelKind kind = oledui::panel_kind() != oledui::PanelKind::kNone
                                     ? oledui::panel_kind()
                                     : oledui::panel_init();
  const bool have_display = kind != oledui::PanelKind::kNone;
  if (!have_display) {
    ESP_LOGW(kTag, "no OLED detected; UI task drives LEDs only");
  } else {
    ESP_LOGI(kTag, "panel kind=%d", static_cast<int>(kind));
  }
  halesp::encoder_init(kPinEncA, kPinEncB, kPinEncSw);
  g_ui_boot_us = esp_timer_get_time();
  halesp::status_leds_init(kPinLedNet, kPinLedBeat, kPinLedRun);

  neon::Config ui_cfg = neon_config();
  neon::MenuModel menu(&ui_cfg);
  neon::Framebuffer fb;
  // Previous frame lives in BSS: two 2 KB framebuffers on an 8 KB stack
  // plus Config would not fit.
  static neon::Framebuffer prev_fb;
  bool have_prev = false;
  bool lead_this = false;

  // Brightness is pushed to the controller only when it changes; a
  // contrast write per frame would waste I2C bandwidth for nothing.
  uint8_t applied_brightness = 0;
  bool brightness_applied = false;

  // Idle display power (docs/SOLAROS_PORTS_HANDOFF.md §3). While
  // blank the render already short-circuits through want_brightness ==
  // 0; input that wakes the panel is dropped so a blind detent cannot
  // nudge tempo.
  neon::ui::IdleDimmer dimmer;
  dimmer.note_activity(esp_timer_get_time());
  bool last_playing = false;

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    dimmer.configure(ui_cfg.display_dim_s, ui_cfg.display_dim_level);
    dimmer.set_playing(last_playing);
    const int64_t poll_us = esp_timer_get_time();
    const bool was_blank =
        dimmer.level(poll_us) == neon::ui::IdleDimmer::Level::kBlank;
    const int detents = halesp::encoder_take_detents();
    if (detents != 0) {
      dimmer.note_activity(poll_us);
      if (!was_blank) {
        menu.on_rotate(detents);
      }
    }
    const int tempo_nudge = menu.take_tempo_nudge();
    if (tempo_nudge != 0) {
      control_queue_push(
          {ControlCommand::Kind::kNudgeTempo, tempo_nudge});
    }
    {
      const halesp::EncoderPress press = halesp::encoder_take_press();
      if (press != halesp::EncoderPress::kNone) {
        dimmer.note_activity(poll_us);
        if (!was_blank) {
          handle_press(menu, press);
        }
      }
    }
    commit_pending_home_short(menu);
    if (boot_locked() && menu.screen() != neon::MenuModel::Screen::kHome) {
      menu.go_home();
    }
    // E0 pot → BPM. Off while no slider is wired: a floating ADC on E0
    // would wander across the pickup threshold and steal tempo.
    constexpr bool kPotDrivesTempo = false;
    if (kPotDrivesTempo) {
      poll_pot_tempo();
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
      live.display_dim_s = ui_cfg.display_dim_s;
      live.display_dim_level = ui_cfg.display_dim_level;
      live.big_beat_display = ui_cfg.big_beat_display;
      live.beat_style = ui_cfg.beat_style;
      live.audio = ui_cfg.audio;
      live.audio_follow_enabled = ui_cfg.audio_follow_enabled;
      live.audio_follow_phase = ui_cfg.audio_follow_phase;
      live.audio_follow_sensitivity = ui_cfg.audio_follow_sensitivity;
      live.audio_follow_input = ui_cfg.audio_follow_input;
      neon_config_apply(live);
      ui_cfg = live;
    } else if (!menu.editing()) {
      // Not mid-edit: adopt whatever the editor or a preset recall wrote.
      ui_cfg = neon_config();
    }

    const uint8_t want_brightness = static_cast<uint8_t>(
        dimmer.apply(ui_cfg.display_brightness, esp_timer_get_time()));
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
    const int64_t now_us = esp_timer_get_time();
    assemble_status(&status, now_us, now_us);
    last_playing = status.playing != 0;
    const uint32_t led_phase = status.phase_milli_beats;
    // LEDs follow Link time. The panel looks a couple of milliseconds
    // ahead on a big redraw so its last rows land on the audible
    // downbeat — I2C writes pages top-to-bottom.
    if (lead_this) {
      neon::TimelineSnapshot tl;
      timeline_bus().read(tl);
      status.phase_milli_beats =
          neon::phase_milli_beats(tl, now_us + neon::ui::kBeatFlushLeadUs);
    }

    halesp::status_led_net(status.active_net != 0);
    halesp::status_led_run(status.playing);
    halesp::status_led_beat((led_phase % 1000) < 150);

    if (have_display && want_brightness != 0) {
      const neon::ui::Layout& lay =
          kind == oledui::PanelKind::kSsd1306I2c ? neon::ui::kLayout64
                                                 : neon::ui::kLayout128;
      neon::render_ui(menu, status, fb, lay);
      if (!oledui::panel_flush(fb)) {
        ESP_LOGW(kTag, "panel flush failed");
      }
      // Clicks during the I2C flush are queued by the encoder task; drain
      // them now so a menu enter/back does not wait another 100 ms frame.
      handle_press(menu, halesp::encoder_take_press());
      commit_pending_home_short(menu);
      const int changed =
          have_prev ? fb.diff_pixels(prev_fb)
                    : neon::Framebuffer::kWidth * neon::Framebuffer::kHeight;
      const bool beat_stage = menu.screen() == neon::MenuModel::Screen::kHome &&
                              status.playing && status.big_beat_display;
      const bool jumps = beat_stage &&
                         status.beat_style !=
                             static_cast<uint8_t>(neon::BeatStyle::kPendulum);
      lead_this = neon::ui::anticipate_beat_flush(jumps, changed);
      prev_fb.copy_from(fb);
      have_prev = true;
    } else {
      lead_this = false;
    }

    if (lead_this) {
      neon::TimelineSnapshot tl;
      timeline_bus().read(tl);
      const int64_t marked = esp_timer_get_time();
      int64_t target =
          marked + neon::us_until_next_beat(tl, marked) - neon::ui::kBeatFlushLeadUs;
      if (target < marked) {
        target = marked;
      }
      const int64_t cap = marked + 100000;  // still poll the encoder at 10 Hz
      if (target > cap) {
        target = cap;
      }
      wait_until_us(target);
      wake = xTaskGetTickCount();
    } else {
      const int hint = dimmer.frame_interval_hint_ms(esp_timer_get_time());
      if (hint > 0) {
        vTaskDelay(pdMS_TO_TICKS(hint));
        wake = xTaskGetTickCount();
      } else {
        vTaskDelayUntil(&wake, kFrameTicks);
      }
    }
  }
}

}  // namespace

void oledui_bringup() {
  const oledui::PanelKind kind = oledui::panel_init();
  if (kind == oledui::PanelKind::kNone) {
    ESP_LOGW(kTag, "bringup: no panel");
    return;
  }
  neon::Framebuffer fb;
  fb.clear();
  oledui::panel_set_brightness(255);
  if (!oledui::panel_flush(fb)) {
    ESP_LOGW(kTag, "bringup: first flush failed (kind=%d)",
             static_cast<int>(kind));
  } else {
    ESP_LOGI(kTag, "bringup: panel kind=%d cleared", static_cast<int>(kind));
  }
}

bool oledui_reinit() {
  oledui::panel_reset();
  oledui_bringup();
  return oledui::panel_kind() != oledui::PanelKind::kNone;
}

const char* oledui_kind_str() {
  switch (oledui::panel_kind()) {
    case oledui::PanelKind::kSsd1327I2c:
      return "ssd1327";
    case oledui::PanelKind::kSh1107I2c:
      return "sh1107";
    case oledui::PanelKind::kSsd1306I2c:
      return "ssd1306";
    case oledui::PanelKind::kSh1107Spi:
      return "sh1107_spi";
    default:
      return "none";
  }
}

void oledui_start() {
  // 128×128 FB (2 KB) + SSD1327 pack buffer lives in panel128; give the
  // UI task room for both.
  xTaskCreatePinnedToCore(ui_task, "oled_ui", 8192, nullptr, 3, nullptr, 0);
}
