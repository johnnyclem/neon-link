#include "sdkconfig.h"
#include "tasks.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "board_mac.h"
#include "board_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/epd_5in79.hpp"
#include "neon/fixed_math.hpp"
#include "neon/gfx/epd_canvas.hpp"
#include "neon/transport.hpp"
#include "neon/ui/epd_front.hpp"
#include "netman/net_manager.h"
#include "provision.h"
#include "wifi.h"

#include "driver/gpio.h"

#include <cstdio>
#include <cstring>

#if CONFIG_NEON_BOARD_LINKSYNC_EPD

namespace {

const char* kTag = "epd";

uint32_t hash_text(uint32_t h, const char* p) {
  for (; p != nullptr && *p != '\0'; ++p) {
    h = h * 33u + static_cast<uint8_t>(*p);
  }
  return h;
}

uint32_t fingerprint(const neon::LinkSyncPanelStatus& s) {
  uint32_t h = s.milli_bpm / 100u;
  h = h * 33u + (s.playing ? 1u : 0u);
  h = h * 33u + s.peers;
  h = h * 33u + (s.provisioned ? 1u : 0u);
  h = h * 33u + (s.wifi_up ? 1u : 0u);
  h = h * 33u + (s.setup_ap ? 1u : 0u);
  h = h * 33u + (s.invert ? 1u : 0u);
  h = h * 33u + s.overlay;
  h = h * 33u + static_cast<uint32_t>(s.cursor);
  h = h * 33u + static_cast<uint32_t>(s.power_cursor);
  h = hash_text(h, s.title);
  h = hash_text(h, s.ssid);
  h = hash_text(h, s.ap_ssid);
  h = hash_text(h, s.ap_pass);
  h = hash_text(h, s.detail);
  for (int i = 0; i < s.n_items && i < 8; ++i) {
    h = hash_text(h, s.item_label[i]);
    h = hash_text(h, s.item_value[i]);
  }
  return h;
}

bool key_down(int pin) {
  return gpio_get_level(static_cast<gpio_num_t>(pin)) == 0;
}

void keys_init() {
  const int pins[] = {kPinKeyUp, kPinKeyDown, kPinKeyTop, kPinKeyBot,
                      kPinKeyOk};
  gpio_config_t io = {};
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pin_bit_mask = 0;
  for (int p : pins) {
    io.pin_bit_mask |= 1ull << static_cast<unsigned>(p);
  }
  gpio_config(&io);
}

void fill_status(neon::LinkSyncPanelStatus* s, neon::EpdFrontPanel& ui) {
  const neon::Config& cfg = neon_config();
  std::snprintf(s->title, sizeof(s->title), "%s", cfg.device_name);
  neon::TimelineSnapshot tl{};
  timeline_bus().read(tl);
  s->milli_bpm = neon::milli_bpm_from_mpb_us(
      tl.tempo_mpb_q32 != 0 ? ((tl.tempo_mpb_q32 + (1ull << 31)) >> 32)
                            : 500000ull);
  s->playing = tl.playing != 0;
  s->peers = tl.num_peers;
  s->provisioned = neon_wifi_has_credentials();
  s->wifi_up = neon_wifi_sta_got_ip();
  s->setup_ap = netman::ap_is_up();
  std::snprintf(s->ssid, sizeof(s->ssid), "%s", neon_wifi_current_ssid());
  std::snprintf(s->ap_pass, sizeof(s->ap_pass), "%s", cfg.ap_pass);
  if (s->setup_ap && netman::ap_ssid()[0] != '\0') {
    std::snprintf(s->ap_ssid, sizeof(s->ap_ssid), "%s", netman::ap_ssid());
  } else {
    uint8_t mac[6] = {};
    neon_read_unit_mac(mac);
    neon::ap_ssid_for(cfg, mac, s->ap_ssid, sizeof(s->ap_ssid));
  }

  const char* trs = cfg.midi_trs_type ? "TRS-B" : "TRS-A";
  const bool show_ap = (s->setup_ap || !s->provisioned) && s->ap_pass[0] != '\0';
  if (show_ap) {
    std::snprintf(s->detail, sizeof(s->detail), "http://192.168.4.1");
  } else if (neon_provision_active()) {
    std::snprintf(s->detail, sizeof(s->detail), "BLE PROV  ESP BLE Prov app");
  } else if (!s->provisioned) {
    std::snprintf(s->detail, sizeof(s->detail), "NO WIFI  wait for SoftAP");
  } else {
    std::snprintf(s->detail, sizeof(s->detail), "MIDI CLOCK  24 PPQN  %s",
                  trs);
  }

  s->invert = ui.invert();
  if (ui.mode() == neon::EpdFrontPanel::Mode::kMenu) {
    s->overlay = 1;
  } else if (ui.mode() == neon::EpdFrontPanel::Mode::kEdit) {
    s->overlay = 2;
  } else if (ui.mode() == neon::EpdFrontPanel::Mode::kPower) {
    s->overlay = 3;
  } else if (ui.mode() == neon::EpdFrontPanel::Mode::kSplash) {
    s->overlay = 4;
  } else {
    s->overlay = 0;
  }
  s->cursor = ui.cursor();
  s->power_cursor = ui.power_cursor();
  s->n_items = neon::EpdFrontPanel::kItems;
  for (int i = 0; i < s->n_items; ++i) {
    std::snprintf(s->item_label[i], sizeof(s->item_label[i]), "%s",
                  ui.item_label(i));
    ui.item_value(i, s->item_value[i], sizeof(s->item_value[i]));
  }
}

void apply_ui_config(const neon::Config& ui_cfg) {
  neon::Config live = neon_config();
  live.quantum_beats = ui_cfg.quantum_beats;
  live.ap_policy = ui_cfg.ap_policy;
  live.start_stop_sync = ui_cfg.start_stop_sync;
  live.midi_clock_out = ui_cfg.midi_clock_out;
  live.midi_trs_type = ui_cfg.midi_trs_type;
  neon_config_apply(live);
}

void paint_clear_then_splash(neon::EpdCanvas* canvas, bool asleep) {
  if (asleep) {
    halesp::epd5in79_awaken();
  }
  halesp::epd5in79_clear();
  neon::LinkSyncPanelStatus st{};
  st.overlay = 4;
  neon::render_linksync_panel(*canvas, st);
  ESP_LOGI(kTag, "splash");
  halesp::epd5in79_display(canvas->data(), /*fast=*/false);
  halesp::epd5in79_sleep();
}

void power_off(neon::EpdCanvas* canvas, bool asleep) {
  ESP_LOGW(kTag, "power off → splash, then deep sleep");
  neon_config_flush_now();
  paint_clear_then_splash(canvas, asleep);
  const uint64_t wake = (1ull << static_cast<unsigned>(kPinKeyTop)) |
                        (1ull << static_cast<unsigned>(kPinKeyBot));
  esp_sleep_enable_ext1_wakeup(wake, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

void epd_task(void*) {
  if (!halesp::epd5in79_init(kPinDispSck, kPinDispMosi, kPinDispCs, kPinDispDc,
                             kPinDispRes, kPinEpdBusy, kPinEpdPwr)) {
    ESP_LOGE(kTag, "e-paper init failed");
    vTaskDelete(nullptr);
    return;
  }
  keys_init();
  neon::Config ui_cfg = neon_config();
  neon::EpdFrontPanel ui(&ui_cfg);
  auto* canvas = new neon::EpdCanvas();
  uint32_t last_fp = 0;
  bool asleep = false;
  int64_t next_ok_us = 0;

  bool raw_top = false;
  bool raw_bot = false;
  bool raw_up = false;
  bool raw_down = false;
  bool raw_ok = false;
  bool top = false;
  bool bot = false;
  bool up = false;
  bool down = false;
  bool ok = false;
  bool prev_top = false;
  bool prev_bot = false;
  bool prev_up = false;
  bool prev_down = false;
  bool prev_ok = false;
  bool chord_armed = false;
  int64_t bot_down_us = 0;
  bool bot_long_fired = false;
  int64_t stable_us = 0;

  for (;;) {
    const int64_t now = esp_timer_get_time();
    const bool s_top = key_down(kPinKeyTop);
    const bool s_bot = key_down(kPinKeyBot);
    const bool s_up = key_down(kPinKeyUp);
    const bool s_down = key_down(kPinKeyDown);
    const bool s_ok = key_down(kPinKeyOk);
    if (s_top != raw_top || s_bot != raw_bot || s_up != raw_up ||
        s_down != raw_down || s_ok != raw_ok) {
      raw_top = s_top;
      raw_bot = s_bot;
      raw_up = s_up;
      raw_down = s_down;
      raw_ok = s_ok;
      stable_us = now;
    }
    if (now - stable_us >= 40000) {
      top = raw_top;
      bot = raw_bot;
      up = raw_up;
      down = raw_down;
      ok = raw_ok;
    }

    bool user = false;
    if (top && bot) {
      if (!chord_armed) {
        ui.on_chord(now);
        chord_armed = true;
        bot_long_fired = true;
        user = true;
        ESP_LOGI(kTag, "chord mode=%d", static_cast<int>(ui.mode()));
      }
    } else {
      chord_armed = false;
      if (up && !prev_up) {
        ui.on_up(now);
        user = true;
      }
      if (down && !prev_down) {
        ui.on_down(now);
        user = true;
      }
      if ((top && !prev_top) || (ok && !prev_ok)) {
        ui.on_confirm(now);
        user = true;
      }
      if (bot && !prev_bot) {
        bot_down_us = now;
        bot_long_fired = false;
      }
      if (!bot && prev_bot && !bot_long_fired) {
        ui.on_cancel(now);
        user = true;
      }
      if (bot && !bot_long_fired && now - bot_down_us >= 3000000) {
        ui.on_cancel_long(now);
        bot_long_fired = true;
        user = true;
        ESP_LOGI(kTag, "power popup");
      }
    }
    prev_top = top;
    prev_bot = bot;
    prev_up = up;
    prev_down = down;
    prev_ok = ok;

    ui.tick(now);
    const int nudge = ui.take_nudge();
    if (nudge != 0) {
      ControlCommand cmd{};
      cmd.kind = ControlCommand::Kind::kNudgeTempo;
      cmd.arg = nudge;
      control_queue_push(cmd);
      user = true;
    }
    if (ui.take_toggle()) {
      ControlCommand cmd{};
      cmd.kind = ControlCommand::Kind::kToggle;
      control_queue_push(cmd);
      user = true;
    }
    if (ui.take_dirty()) {
      apply_ui_config(ui_cfg);
      ui_cfg = neon_config();
      user = true;
    } else if (ui.mode() == neon::EpdFrontPanel::Mode::kLive ||
               ui.mode() == neon::EpdFrontPanel::Mode::kMenu) {
      ui_cfg = neon_config();
    }
    neon_config_flush(now);

    const auto act = ui.take_action();
    if (act == neon::EpdFrontPanel::Action::kReboot) {
      neon_config_flush_now();
      ESP_LOGW(kTag, "restart after splash");
      paint_clear_then_splash(canvas, asleep);
      esp_restart();
    }
    if (act == neon::EpdFrontPanel::Action::kPowerOff) {
      power_off(canvas, asleep);
    }

    neon::LinkSyncPanelStatus st{};
    fill_status(&st, ui);
    const uint32_t fp = fingerprint(st);
    if (fp != last_fp && (user || now >= next_ok_us)) {
      if (asleep) {
        halesp::epd5in79_awaken();
        asleep = false;
      }
      neon::render_linksync_panel(*canvas, st);
      ESP_LOGI(kTag, "paint overlay=%u invert=%d cursor=%d",
               static_cast<unsigned>(st.overlay), st.invert ? 1 : 0,
               st.cursor);
      halesp::epd5in79_display(canvas->data(), /*fast=*/false);
      halesp::epd5in79_sleep();
      asleep = true;
      last_fp = fp;
      next_ok_us = now + (user ? 800000 : 5000000);
    }
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

}  // namespace

void neon_start_epd_service() {
  xTaskCreatePinnedToCore(epd_task, "epd", 8192, nullptr, 3, nullptr, 0);
}

#else

void neon_start_epd_service() {}

#endif
