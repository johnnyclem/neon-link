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
#include "halesp/rlcd_st7305.hpp"
#include "neon/fixed_math.hpp"
#include "neon/gfx/rlcd_canvas.hpp"
#include "neon/gfx/rlcd_pack.hpp"
#include "neon/timeline.hpp"
#include "neon/transport.hpp"
#include "neon/ui/rlcd_front.hpp"
#include "neon/ui/rlcd_refresh.hpp"
#include "netman/net_manager.h"
#include "provision.h"
#include "wifi.h"

#include "driver/gpio.h"

#if CONFIG_NEON_BOARD_LINKSYNC_RLCD
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#endif

#include <cstdio>
#include <cstring>

#if CONFIG_NEON_BOARD_LINKSYNC_RLCD

// Live status face for the Waveshare ESP32-S3-RLCD-4.2. The ST7305
// reflective panel repaints in milliseconds with no flash, so unlike
// the e-paper face this one shows the beat as it happens; the planner
// keeps the SPI quiet and drops the scan to 1 Hz LPM when nothing
// moves. Two buttons: the side KEY and BOOT (see RlcdFrontPanel).

namespace {

const char* kTag = "rlcd";

constexpr int64_t kLongPressUs = 500000;
constexpr int64_t kDebounceUs = 30000;
constexpr int64_t kBatteryPeriodUs = 2000000;
// Auto-repeat once a button is held past the long-press threshold: a
// lead-in, then a slow cadence that accelerates. Only the Tempo screen
// acts on these, where holding walks the BPM up or down.
constexpr int64_t kRepeatLeadUs = 300000;
constexpr int64_t kRepeatSlowUs = 150000;
constexpr int64_t kRepeatFastUs = 60000;
constexpr int kRepeatAccelAfter = 5;
// Hold KEY+BOOT together this long to flip landscape<->portrait. Blind
// gesture (no accelerometer, PWR is not readable): long enough that the
// two control buttons are never both held this long by accident, and a
// "keep holding" cue appears partway through.
constexpr int64_t kOrientHoldUs = 3000000;
constexpr int64_t kOrientHintUs = 700000;

uint32_t hash_text(uint32_t h, const char* p) {
  for (; p != nullptr && *p != '\0'; ++p) {
    h = h * 33u + static_cast<uint8_t>(*p);
  }
  return h;
}

uint32_t fingerprint(const neon::RlcdPanelStatus& rs) {
  const neon::LinkSyncPanelStatus& s = rs.base;
  uint32_t h = s.milli_bpm / 100u;
  h = h * 33u + (s.playing ? 1u : 0u);
  h = h * 33u + s.peers;
  h = h * 33u + (s.provisioned ? 1u : 0u);
  h = h * 33u + (s.wifi_up ? 1u : 0u);
  h = h * 33u + (s.setup_ap ? 1u : 0u);
  h = h * 33u + s.overlay;
  h = h * 33u + static_cast<uint32_t>(s.cursor);
  h = h * 33u + static_cast<uint32_t>(s.power_cursor);
  h = h * 33u + rs.beat;
  h = h * 33u + rs.quantum;
  h = h * 33u + rs.theme;
  h = h * 33u + (s.invert ? 1u : 0u);
  // Quantize so ADC jitter never repaints the gauge.
  h = h * 33u + static_cast<uint32_t>(rs.battery_pct < 0
                                          ? 0x7fffffff
                                          : rs.battery_pct / 5);
  h = hash_text(h, s.title);
  h = hash_text(h, s.ssid);
  h = hash_text(h, s.ap_ssid);
  h = hash_text(h, s.ap_pass);
  h = hash_text(h, s.detail);
  for (int i = 0; i < s.n_items && i < 10; ++i) {
    h = hash_text(h, s.item_label[i]);
    h = hash_text(h, s.item_value[i]);
  }
  return h;
}

// Debounced button: short fires on release, long fires once at the hold
// threshold (so the menu/tempo screen opens while the finger is still
// down, like every appliance the user already owns), and repeat fires
// on a cadence while the hold continues past that — the Tempo screen
// uses it to ramp the BPM.
class Button {
 public:
  explicit Button(int pin) : pin_(pin) {}

  void poll(int64_t now, bool* short_fire, bool* long_fire,
            bool* repeat_fire) {
    *short_fire = false;
    *long_fire = false;
    *repeat_fire = false;
    const bool raw =
        gpio_get_level(static_cast<gpio_num_t>(pin_)) == 0;  // active low
    if (raw != raw_) {
      raw_ = raw;
      edge_us_ = now;
    }
    if (now - edge_us_ < kDebounceUs) {
      return;
    }
    if (raw_ && !down_) {
      down_ = true;
      down_us_ = now;
      long_fired_ = false;
      repeats_ = 0;
    } else if (raw_ && down_ && !long_fired_ && now - down_us_ >= kLongPressUs) {
      long_fired_ = true;
      *long_fire = true;
      next_repeat_us_ = now + kRepeatLeadUs;
    } else if (raw_ && down_ && long_fired_ && now >= next_repeat_us_) {
      *repeat_fire = true;
      ++repeats_;
      next_repeat_us_ =
          now + (repeats_ >= kRepeatAccelAfter ? kRepeatFastUs : kRepeatSlowUs);
    } else if (!raw_ && down_) {
      down_ = false;
      if (!long_fired_) {
        *short_fire = true;
      }
    }
  }

  int pin() const { return pin_; }

  // Drop any in-progress press without emitting an event. Used when the
  // KEY+BOOT chord takes over: the button that was pressed first must not
  // fire a stray short/long/tempo once the chord releases.
  void reset() {
    raw_ = false;
    down_ = false;
    long_fired_ = false;
    repeats_ = 0;
    edge_us_ = 0;
    down_us_ = 0;
    next_repeat_us_ = 0;
  }

 private:
  int pin_;
  bool raw_ = false;
  bool down_ = false;
  bool long_fired_ = false;
  int repeats_ = 0;
  int64_t edge_us_ = 0;
  int64_t down_us_ = 0;
  int64_t next_repeat_us_ = 0;
};

class Battery {
 public:
  void init() {
    adc_oneshot_unit_init_cfg_t unit = {};
    unit.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&unit, &adc_) != ESP_OK) {
      adc_ = nullptr;
      return;
    }
    adc_oneshot_chan_cfg_t chan = {};
    chan.atten = ADC_ATTEN_DB_12;
    chan.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(adc_, kChannel, &chan);
    adc_cali_curve_fitting_config_t cali = {};
    cali.unit_id = ADC_UNIT_1;
    cali.chan = kChannel;
    cali.atten = ADC_ATTEN_DB_12;
    cali.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali, &cali_) != ESP_OK) {
      cali_ = nullptr;
    }
  }

  // VBAT through the board's ÷3 divider on GPIO4. 0 = unavailable.
  uint32_t millivolts() {
    if (adc_ == nullptr) {
      return 0;
    }
    int raw = 0;
    if (adc_oneshot_read(adc_, kChannel, &raw) != ESP_OK) {
      return 0;
    }
    int mv = 0;
    if (cali_ != nullptr && adc_cali_raw_to_voltage(cali_, raw, &mv) == ESP_OK) {
      return static_cast<uint32_t>(mv) * 3u;
    }
    // Uncalibrated fallback: 12-bit full scale ≈ 3100 mV at 12 dB.
    return static_cast<uint32_t>(raw) * 3100u / 4095u * 3u;
  }

 private:
  // GPIO4 = ADC1 channel 3 on the ESP32-S3.
  static constexpr adc_channel_t kChannel = ADC_CHANNEL_3;
  adc_oneshot_unit_handle_t adc_ = nullptr;
  adc_cali_handle_t cali_ = nullptr;
};

int battery_percent(uint32_t mv) {
  if (mv == 0) {
    return -1;
  }
  // Coarse 1S LiPo resting curve; good enough for a panel gauge.
  static const struct {
    uint32_t mv;
    int pct;
  } kCurve[] = {{4150, 100}, {4000, 80}, {3850, 60},
                {3750, 40},  {3650, 20}, {3450, 5},
                {3300, 0}};
  if (mv >= kCurve[0].mv) {
    return 100;
  }
  for (size_t i = 1; i < sizeof(kCurve) / sizeof(kCurve[0]); ++i) {
    if (mv >= kCurve[i].mv) {
      const uint32_t span = kCurve[i - 1].mv - kCurve[i].mv;
      const int steps = kCurve[i - 1].pct - kCurve[i].pct;
      return kCurve[i].pct +
             static_cast<int>((mv - kCurve[i].mv) * steps / span);
    }
  }
  return 0;
}

void fill_status(neon::RlcdPanelStatus* rs, neon::RlcdFrontPanel& ui,
                 int64_t now) {
  neon::LinkSyncPanelStatus* s = &rs->base;
  const neon::Config& cfg = neon_config();
  std::snprintf(s->title, sizeof(s->title), "%s", cfg.device_name);
  neon::TimelineSnapshot tl{};
  timeline_bus().read(tl);
  s->milli_bpm = neon::milli_bpm_from_mpb_us(
      tl.tempo_mpb_q32 != 0 ? ((tl.tempo_mpb_q32 + (1ull << 31)) >> 32)
                            : 500000ull);
  s->playing = tl.playing != 0;
  s->peers = tl.num_peers;
  rs->quantum = tl.quantum_beats != 0 ? tl.quantum_beats : 4;
  rs->beat = s->playing
                 ? neon::beat_number(neon::phase_milli_beats(tl, now),
                                     rs->quantum)
                 : 0;
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

  if (ui.mode() == neon::RlcdFrontPanel::Mode::kMenu) {
    s->overlay = 1;
  } else if (ui.mode() == neon::RlcdFrontPanel::Mode::kEdit) {
    s->overlay = 2;
  } else if (ui.mode() == neon::RlcdFrontPanel::Mode::kPower) {
    s->overlay = 3;
  } else if (ui.mode() == neon::RlcdFrontPanel::Mode::kSplash) {
    s->overlay = 4;
  } else if (ui.mode() == neon::RlcdFrontPanel::Mode::kTempo) {
    s->overlay = 5;
  } else {
    s->overlay = 0;
  }
  s->cursor = ui.cursor();
  s->power_cursor = ui.power_cursor();
  // Theme + dark flag come from the committed config, not the copy being
  // edited — the face changes when the edit commits, and the menu overlay
  // flips dark with it so the whole UI reads as one piece.
  rs->theme = static_cast<uint8_t>(cfg.mono_theme);
  s->invert = neon::mono_theme_dark(cfg.mono_theme);
  s->n_items = neon::RlcdFrontPanel::kItems;
  for (int i = 0; i < s->n_items && i < 10; ++i) {
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
  live.display_portrait = ui_cfg.display_portrait;
  live.mono_theme = ui_cfg.mono_theme;
  neon_config_apply(live);
}

void keys_init() {
  const int pins[] = {kPinKeyUser, kPinKeyBoot};
  gpio_config_t io = {};
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pin_bit_mask = 0;
  for (int p : pins) {
    io.pin_bit_mask |= 1ull << static_cast<unsigned>(p);
  }
  gpio_config(&io);
}

// Full-screen "keep holding to rotate" cue with a countdown, centered for
// the current canvas orientation.
void render_orient_hint(neon::RlcdCanvas& c, bool to_portrait, int secs) {
  c.clear();
  const int w = c.width();
  const int h = c.height();
  auto center = [&](int y, const char* str, int sc) {
    const int tw = static_cast<int>(std::strlen(str)) * 6 * sc;
    c.draw_text((w - tw) / 2, y, str, sc);
  };
  center(h / 2 - 76, "ROTATING TO", 2);
  center(h / 2 - 40, to_portrait ? "PORTRAIT" : "LANDSCAPE", 4);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "HOLD %d", secs < 0 ? 0 : secs);
  center(h / 2 + 24, buf, 3);
  center(h / 2 + 74, "RELEASE TO CANCEL", 2);
}

void power_off(neon::RlcdCanvas* canvas, uint8_t* packed) {
  ESP_LOGW(kTag, "power off → splash, then deep sleep");
  neon_config_flush_now();
  neon::render_rlcd_splash(*canvas);
  neon::rlcd::pack_frame(canvas->data(), packed);
  halesp::rlcd_st7305_present(packed);
  // Leave the splash on the glass: LPM scan holds the image for
  // microamps while the ESP deep-sleeps. KEY wakes the box.
  halesp::rlcd_st7305_set_power(false);
  const uint64_t wake = 1ull << static_cast<unsigned>(kPinKeyUser);
  esp_sleep_enable_ext1_wakeup(wake, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

void rlcd_task(void*) {
  if (!halesp::rlcd_st7305_init(kPinDispSck, kPinDispMosi, kPinDispCs,
                                kPinDispDc, kPinDispRes)) {
    ESP_LOGE(kTag, "ST7305 init failed");
    vTaskDelete(nullptr);
    return;
  }
  keys_init();
  Battery battery;
  battery.init();

  neon::Config ui_cfg = neon_config();
  neon::RlcdFrontPanel ui(&ui_cfg);
  neon::RlcdFramePlanner planner;
  auto* canvas = new neon::RlcdCanvas();
  auto* packed = new uint8_t[neon::rlcd::kPackedSize];
  Button key(kPinKeyUser);
  Button boot(kPinKeyBoot);
  uint32_t last_fp = 0;
  bool have_fp = false;
  bool hpm = true;
  uint32_t battery_mv = 0;
  int64_t battery_us = -kBatteryPeriodUs;

  auto orient_of = [](const neon::Config& c) {
    return c.display_portrait ? neon::RlcdCanvas::Orientation::kPortrait
                              : neon::RlcdCanvas::Orientation::kLandscape;
  };
  canvas->set_orientation(orient_of(ui_cfg));

  // KEY+BOOT chord state: kIdle polls normally; kChord counts the hold and
  // fires once; kDrain waits for both buttons to fully release so the chord
  // does not spill a stray press into normal input.
  enum ChordState { kIdle, kChord, kDrain };
  ChordState chord = kIdle;
  int64_t chord_since = 0;
  bool chord_fired = false;

  for (;;) {
    const int64_t now = esp_timer_get_time();
    bool user = false;

    // Orientation chord: both control buttons held together. While a chord
    // is active (or draining) the individual button state machines are held
    // reset, so no menu/tempo/transport event escapes.
    const bool key_raw =
        gpio_get_level(static_cast<gpio_num_t>(key.pin())) == 0;
    const bool boot_raw =
        gpio_get_level(static_cast<gpio_num_t>(boot.pin())) == 0;
    bool orient_hint = false;
    if (key_raw && boot_raw) {
      if (chord != kChord) {
        chord = kChord;
        chord_since = now;
        chord_fired = false;
      }
      key.reset();
      boot.reset();
      user = true;
      planner.note_user(now);
      if (!chord_fired && now - chord_since >= kOrientHoldUs) {
        chord_fired = true;
        ui_cfg.display_portrait = ui_cfg.display_portrait ? 0 : 1;
        canvas->set_orientation(orient_of(ui_cfg));
        ui.show_live(now);
        neon::Config live = neon_config();
        live.display_portrait = ui_cfg.display_portrait;
        neon_config_apply(live);
        ui_cfg = neon_config();
        have_fp = false;  // force a full repaint in the new orientation
        ESP_LOGI(kTag, "orientation -> %s",
                 ui_cfg.display_portrait ? "portrait" : "landscape");
      } else if (!chord_fired && now - chord_since >= kOrientHintUs) {
        orient_hint = true;
      }
    } else if (chord != kIdle) {
      // A chord just ended: swallow input until both buttons are released.
      key.reset();
      boot.reset();
      if (!key_raw && !boot_raw) {
        chord = kIdle;
      } else {
        chord = kDrain;
      }
    } else {
      bool ks = false, kl = false, kr = false;
      bool bs = false, bl = false, br = false;
      key.poll(now, &ks, &kl, &kr);
      boot.poll(now, &bs, &bl, &br);
      if (ks) ui.on_key_short(now);
      if (kl) ui.on_key_long(now);
      if (kr) ui.on_key_repeat(now);
      if (bs) ui.on_boot_short(now);
      if (bl) ui.on_boot_long(now);
      if (br) ui.on_boot_repeat(now);
      if (ks || kl || kr || bs || bl || br) {
        user = true;
        planner.note_user(now);
      }
    }

    // While the chord is being held past the hint threshold, take over the
    // glass with a live "keep holding" cue and a countdown, drawn in the
    // orientation we are about to switch TO so it reads upright in the stand
    // the user is turning the panel toward. The flip is the confirmation.
    if (orient_hint) {
      const int secs =
          static_cast<int>((kOrientHoldUs - (now - chord_since)) / 1000000) + 1;
      const auto cur = canvas->orientation();
      const auto target =
          ui_cfg.display_portrait ? neon::RlcdCanvas::Orientation::kLandscape
                                  : neon::RlcdCanvas::Orientation::kPortrait;
      canvas->set_orientation(target);
      halesp::rlcd_st7305_set_power(true);
      hpm = true;
      render_orient_hint(
          *canvas, target == neon::RlcdCanvas::Orientation::kPortrait, secs);
      neon::rlcd::pack_frame(canvas->data(), packed);
      halesp::rlcd_st7305_present(packed);
      canvas->set_orientation(cur);  // restore; the real flip is on fire
      have_fp = false;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

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
    } else if (ui.mode() == neon::RlcdFrontPanel::Mode::kLive ||
               ui.mode() == neon::RlcdFrontPanel::Mode::kMenu) {
      ui_cfg = neon_config();
    }
    neon_config_flush(now);

    // Keep the panel orientation in step with config — covers a change made
    // in the web editor as well as the chord toggle above.
    if (canvas->orientation() != orient_of(ui_cfg)) {
      canvas->set_orientation(orient_of(ui_cfg));
      have_fp = false;
    }

    const auto act = ui.take_action();
    if (act == neon::RlcdFrontPanel::Action::kReboot) {
      neon_config_flush_now();
      ESP_LOGW(kTag, "restart");
      esp_restart();
    }
    if (act == neon::RlcdFrontPanel::Action::kPowerOff) {
      power_off(canvas, packed);
    }

    if (now - battery_us >= kBatteryPeriodUs) {
      battery_us = now;
      const uint32_t mv = battery.millivolts();
      // Light EMA so the gauge doesn't wander with load spikes.
      battery_mv = battery_mv == 0 ? mv : (battery_mv * 3 + mv) / 4;
    }

    neon::RlcdPanelStatus st{};
    fill_status(&st, ui, now);
    st.battery_mv = battery_mv;
    st.battery_pct = battery_percent(battery_mv);
    st.low_power = !hpm;

    const uint32_t fp = fingerprint(st);
    const bool changed = !have_fp || fp != last_fp;
    if (planner.plan(now, changed, user)) {
      halesp::rlcd_st7305_set_power(true);
      hpm = true;
      neon::render_rlcd_panel(*canvas, st);
      neon::rlcd::pack_frame(canvas->data(), packed);
      halesp::rlcd_st7305_present(packed);
      planner.note_painted(now);
      last_fp = fp;
      have_fp = true;
    }
    const bool want_hpm =
        planner.power(now) == neon::RlcdFramePlanner::Power::kHpm;
    if (want_hpm != hpm) {
      halesp::rlcd_st7305_set_power(want_hpm);
      hpm = want_hpm;
      ESP_LOGI(kTag, "%s", hpm ? "HPM 32Hz" : "LPM 1Hz");
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

void neon_start_rlcd_service() {
  xTaskCreatePinnedToCore(rlcd_task, "rlcd", 8192, nullptr, 3, nullptr, 0);
}

#else

void neon_start_rlcd_service() {}

#endif
