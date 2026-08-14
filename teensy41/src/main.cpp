// NEON LINK on a Teensy 4.1: 2.8" ILI9341/XPT2046 touchscreen, two
// rotary encoders, six pulse outputs, Tempo CV, 16 MB PSRAM.
//
// Same portable core as the ESP32-S3 targets — MenuModel, render_ui,
// MultiClockEngine — driven from the Arduino loop instead of FreeRTOS
// tasks. The timeline is internal for now (tempo encoder + transport
// switch); the Link port over the Teensy's native Ethernet is the
// tracked follow-up (docs/TEENSY41.md §7).
//
// Controls:
//   ENC1  rotate = navigate/edit   click = enter/confirm   hold = back
//   ENC2  rotate = tempo ±1 BPM    click = start/stop
//   touch strip: + / - mirror ENC1 rotate, OK = click, BACK = hold
//   tap on the UI zone = click

#include <Arduino.h>

#include "board_pins_t41.h"
#include "config_store_t41.h"
#include "display_t41.h"
#include "encoders_t41.h"
#include "internal_timeline.h"
#include "psram_t41.h"
#include "pulse_hw_t41.h"
#include "timebase_t41.h"
#include "touch_t41.h"

#include "neon/gfx/framebuffer.hpp"
#include "neon/multi_engine.hpp"
#include "neon/transport.hpp"
#include "neon/ui/icons_gen.hpp"
#include "neon/ui/menu_model.hpp"
#include "neon/ui/render.hpp"

namespace {

// Engine scheduling, same cadence as the ESP32 core-1 task: 5 ms refill,
// 15 ms horizon, 2 ms minimum lead over the emitter.
constexpr int64_t kHorizonUs = 15000;
constexpr int64_t kLeadUs = 2000;
constexpr uint32_t kRefillMs = 5;
constexpr uint32_t kFrameMs = 33;      // ~30 fps UI
constexpr uint32_t kSaveDebounceMs = 2000;

neon::Config g_cfg;
neon::MenuModel g_menu(&g_cfg);
neon::Framebuffer g_fb;
neon::MultiClockEngine g_engine;
InternalTimeline g_timeline;
PulseHwT41 g_pulse_hw;
DisplayT41 g_display;
TouchT41 g_touch;

int64_t g_cursor = 0;
uint32_t g_timeline_version = ~0u;
uint32_t g_next_refill_ms = 0;
uint32_t g_next_frame_ms = 0;
uint32_t g_save_at_ms = 0;   // 0 = nothing pending
uint8_t g_backlight = 255;

void schedule_save(uint32_t now_ms) { g_save_at_ms = now_ms + kSaveDebounceMs; }

void apply_engine_config(int64_t now_us) {
  neon::EngineConfig eng = g_cfg.engine;
  eng.quantum_beats = g_cfg.quantum_beats;
  g_engine.set_config(eng);
  g_engine.retime(g_timeline.snapshot(), now_us + kLeadUs);
  g_cursor = t41_now_us() + kLeadUs;
  g_timeline_version = g_timeline.version();
}

void update_tempo_cv() {
  // FlexPWM duty -> RC filter -> 0-5 V. Config maps the BPM span.
  const int32_t bpm = static_cast<int32_t>(g_timeline.milli_bpm() / 1000);
  const int32_t lo = g_cfg.tempo_cv_min_bpm;
  const int32_t hi =
      g_cfg.tempo_cv_max_bpm > lo ? g_cfg.tempo_cv_max_bpm : lo + 1;
  int32_t duty = (bpm - lo) * 255 / (hi - lo);
  if (duty < 0) duty = 0;
  if (duty > 255) duty = 255;
  analogWrite(kPinTempoCv, duty);
}

void handle_menu_click() { g_menu.on_click(); }
void handle_menu_back() { g_menu.on_long_press(); }

void handle_transport_toggle(int64_t now_us) {
  g_timeline.set_playing(!g_timeline.playing(), now_us);
}

void handle_tempo_nudge(int detents, int64_t now_us, uint32_t now_ms) {
  const uint32_t next =
      neon::nudge_milli_bpm(g_timeline.milli_bpm(), detents);
  g_timeline.set_tempo(next, now_us);
  g_cfg.tempo_milli_bpm = next;
  update_tempo_cv();
  schedule_save(now_ms);
}

void service_inputs(int64_t now_us, uint32_t now_ms) {
  // ENC1: the menu encoder.
  const int d1 = enc::take_detents(0);
  if (d1 != 0) {
    g_menu.on_rotate(d1);
  }
  switch (enc::poll_button(0, now_ms)) {
    case enc::ButtonEvent::kClick: handle_menu_click(); break;
    case enc::ButtonEvent::kLongPress: handle_menu_back(); break;
    default: break;
  }

  // ENC2: tempo / transport.
  const int d2 = enc::take_detents(1);
  if (d2 != 0) {
    handle_tempo_nudge(d2, now_us, now_ms);
  }
  if (enc::poll_button(1, now_ms) == enc::ButtonEvent::kClick) {
    handle_transport_toggle(now_us);
  }

  // Touchscreen.
  switch (g_touch.poll(now_ms)) {
    case TouchT41::Event::kPlus: g_menu.on_rotate(1); break;
    case TouchT41::Event::kMinus: g_menu.on_rotate(-1); break;
    case TouchT41::Event::kOk: handle_menu_click(); break;
    case TouchT41::Event::kBack: handle_menu_back(); break;
    default: break;
  }

  // Menu side effects.
  if (g_menu.take_dirty()) {
    apply_engine_config(now_us);
    update_tempo_cv();
    schedule_save(now_ms);
  }
  if (g_menu.take_action() == neon::MenuModel::Action::kReboot) {
    cfgstore::save(g_cfg);
    Serial.flush();
    SCB_AIRCR = 0x05FA0004;  // SYSRESETREQ
    for (;;) {
    }
  }
}

void service_engine(int64_t now_us) {
  if (g_timeline.version() != g_timeline_version) {
    g_timeline_version = g_timeline.version();
    g_engine.retime(g_timeline.snapshot(), g_cursor);
  }
  const int64_t until = now_us + kLeadUs + kHorizonUs;
  if (until <= g_cursor) {
    return;
  }
  neon::Edge edges[64];
  size_t n;
  do {
    n = g_engine.generate(g_cursor, until, edges,
                          sizeof(edges) / sizeof(edges[0]));
    for (size_t i = 0; i < n; ++i) {
      const uint32_t mask = 1u << edges[i].channel;
      const hal::PulseEdge pe{edges[i].t_us, edges[i].high ? mask : 0u,
                              edges[i].high ? 0u : mask};
      while (!g_pulse_hw.submit(pe)) {
        // Ring full: the 100 µs ISR is draining it; yield briefly.
        delayMicroseconds(50);
      }
    }
  } while (n == sizeof(edges) / sizeof(edges[0]));
  g_cursor = until;
}

void assemble_status(neon::UiStatus* s, int64_t now_us) {
  const neon::TimelineSnapshot& tl = g_timeline.snapshot();
  s->milli_bpm = g_timeline.milli_bpm();
  s->tempo_valid = true;  // the internal grid always has a tempo
  s->playing = g_timeline.playing();
  s->quantum_beats = tl.quantum_beats;
  s->phase_milli_beats = neon::phase_milli_beats(tl, now_us);
  s->anim_tick =
      static_cast<uint32_t>(now_us / (1000000 / neon::ui::kIconTickHz));
  s->active_net = 0;  // no networking on this target yet
  s->peers = 0;
  s->ext_clock = false;
  s->setup_ap = false;
  s->ble_on = false;
  s->big_beat_display = g_cfg.big_beat_display != 0;
  s->ip[0] = '\0';
}

void service_ui(int64_t now_us) {
  neon::UiStatus status;
  assemble_status(&status, now_us);
  neon::render_ui(g_menu, status, g_fb);
  g_display.draw_ui(g_fb);
  g_display.draw_strip(g_touch.pressed_mask(), status.playing);

  if (g_cfg.display_brightness != g_backlight) {
    g_backlight = g_cfg.display_brightness;
    g_display.set_backlight(g_backlight);
  }

  // LEDs: no network yet; beat flashes the first ~12% of every beat;
  // run mirrors the engine's Run gate.
  digitalWriteFast(kPinLedNet, LOW);
  const bool beat_on =
      status.playing && (status.phase_milli_beats % 1000u) < 120u;
  digitalWriteFast(kPinLedBeat, beat_on ? HIGH : LOW);
  digitalWriteFast(kPinLedRun,
                   (PulseHwT41::levels() & (1u << neon::kChRun)) ? HIGH : LOW);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  // Don't block on the monitor, but give it a beat so the PSRAM report
  // is visible when one is attached.
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 1500;) {
  }

  const psram::Report ps = psram::init(16);
  Serial.printf("NEON LINK teensy41  psram=%luMB test=%s\n",
                static_cast<unsigned long>(ps.megabytes),
                ps.megabytes == 0 ? "absent"
                : ps.test_ok      ? "ok"
                                  : "FAIL");
  if (ps.megabytes != 0 && !ps.test_ok) {
    Serial.printf("  first failing address 0x%08lx\n",
                  static_cast<unsigned long>(ps.fail_addr));
  }

  pinMode(kPinLedNet, OUTPUT);
  pinMode(kPinLedBeat, OUTPUT);
  pinMode(kPinLedRun, OUTPUT);
  pinMode(kPinClkIn, INPUT);
  pinMode(kPinRstIn, INPUT);
  analogWriteFrequency(kPinTempoCv, 20000);  // well above the RC corner

  if (!cfgstore::load(&g_cfg)) {
    Serial.println("config: defaults (EEPROM empty or stale)");
  }
  g_backlight = g_cfg.display_brightness;

  g_display.init();
  g_display.set_backlight(g_backlight);
  g_display.draw_strip(0, false, /*force=*/true);
  g_touch.init();
  enc::init();
  if (!g_pulse_hw.init()) {
    Serial.println("pulse timer init FAILED");
  }

  const int64_t now = t41_now_us();
  g_timeline.init(g_cfg.tempo_milli_bpm, g_cfg.quantum_beats, now);
  apply_engine_config(now);
  update_tempo_cv();
}

void loop() {
  const int64_t now_us = t41_now_us();
  const uint32_t now_ms = millis();

  service_inputs(now_us, now_ms);

  if (static_cast<int32_t>(now_ms - g_next_refill_ms) >= 0) {
    g_next_refill_ms = now_ms + kRefillMs;
    service_engine(now_us);
  }

  if (static_cast<int32_t>(now_ms - g_next_frame_ms) >= 0) {
    g_next_frame_ms = now_ms + kFrameMs;
    service_ui(now_us);
  }

  if (g_save_at_ms != 0 && static_cast<int32_t>(now_ms - g_save_at_ms) >= 0) {
    g_save_at_ms = 0;
    cfgstore::save(g_cfg);
    Serial.println("config: saved");
  }
}
