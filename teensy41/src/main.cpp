// NEON LINK on a Teensy 4.1 — full-service build: Ableton Link over
// native Ethernet, the web editor, TRS MIDI clock, CLK/RST IN external
// clock following, the audio engine on an SGTL5000 shield, a 2.8"
// ILI9341/XPT2046 touchscreen, two rotary encoders, six pulse outputs,
// Tempo CV, and 16 MB PSRAM.
//
// Same portable core and service shapes as the ESP32-S3 firmware — the
// FreeRTOS tasks become polled services on the Arduino loop, and the
// real-time emitters (pulse edges, MIDI clock, audio blocks) run in
// interrupts so a slow display push can never smear an output.
//
// Controls:
//   ENC1  rotate = navigate/edit   click = enter/confirm   hold = back
//   ENC2  rotate = tempo ±1 BPM    click = start/stop (quantized)
//   touch strip: + / - mirror ENC1 rotate, OK = click, BACK = hold
//   tap on the UI zone = click
//
// No radio on this hardware: WiFi, the setup AP, and BLE MIDI live on
// the ESP32 targets only (docs/TEENSY41.md §6).

#include <Arduino.h>

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "neon/gfx/framebuffer.hpp"
#include "neon/multi_engine.hpp"
#include "neon/transport.hpp"
#include "neon/ui/icons_gen.hpp"
#include "neon/ui/menu_model.hpp"
#include "neon/ui/render.hpp"

#include "audio_t41.h"
#include "board_pins_t41.h"
#include "display_t41.h"
#include "encoders_t41.h"
#include "httpd_t41.h"
#include "link_service_t41.h"
#include "midi_t41.h"
#include "net_t41.h"
#include "psram_t41.h"
#include "pulse_hw_t41.h"
#include "timebase_t41.h"
#include "touch_t41.h"

namespace {

// Engine scheduling: 15 ms refill with a 60 ms horizon. The horizon
// outlasts the worst-case UI stall (a full 240x240 frame push is ~30 ms
// of blocking SPI), so the interrupt emitter never starves.
constexpr int64_t kHorizonUs = 60000;
constexpr int64_t kLeadUs = 5000;
constexpr uint32_t kRefillMs = 15;
constexpr uint32_t kFrameMs = 33;  // ~30 fps UI

neon::Config g_ui_cfg;  // the menu's working copy (synced by config rev)
neon::MenuModel g_menu(&g_ui_cfg);
neon::Framebuffer g_fb;
neon::MultiClockEngine g_engine;
PulseHwT41 g_pulse_hw;
DisplayT41 g_display;
TouchT41 g_touch;

int64_t g_cursor = 0;
uint32_t g_timeline_version = 0;
uint32_t g_engine_cfg_version = 0;
bool g_have_timeline = false;
neon::TimelineSnapshot g_last_snap{};
uint32_t g_ui_cfg_rev = ~0u;
uint32_t g_next_refill_ms = 0;
uint32_t g_next_frame_ms = 0;
uint8_t g_backlight = 255;
int64_t g_reboot_at_us = 0;

void do_reboot() {
  neon_config_flush_now();
  Serial.flush();
  SCB_AIRCR = 0x05FA0004;  // SYSRESETREQ
  for (;;) {
  }
}

// Keep the menu's working copy in step with edits from the web editor
// (and preset recalls); never yank it mid-edit.
void sync_ui_config() {
  if (neon_config_rev() != g_ui_cfg_rev && !g_menu.editing()) {
    g_ui_cfg = neon_config();
    g_ui_cfg_rev = neon_config_rev();
  }
}

void service_inputs(int64_t now_us, uint32_t now_ms) {
  // ENC1: the menu encoder.
  const int d1 = enc::take_detents(0);
  if (d1 != 0) {
    g_menu.on_rotate(d1);
  }
  switch (enc::poll_button(0, now_ms)) {
    case enc::ButtonEvent::kClick: g_menu.on_click(); break;
    case enc::ButtonEvent::kLongPress: g_menu.on_long_press(); break;
    default: break;
  }

  // ENC2: tempo / transport, routed through the control queue so the
  // Link session keeps its single owner (the link service).
  const int d2 = enc::take_detents(1);
  if (d2 != 0) {
    control_queue_push({ControlCommand::Kind::kNudgeTempo, d2});
  }
  if (enc::poll_button(1, now_ms) == enc::ButtonEvent::kClick) {
    control_queue_push({ControlCommand::Kind::kToggle, 0});
  }

  // Touchscreen.
  switch (g_touch.poll(now_ms)) {
    case TouchT41::Event::kPlus: g_menu.on_rotate(1); break;
    case TouchT41::Event::kMinus: g_menu.on_rotate(-1); break;
    case TouchT41::Event::kOk: g_menu.on_click(); break;
    case TouchT41::Event::kBack: g_menu.on_long_press(); break;
    default: break;
  }

  // Menu side effects.
  if (g_menu.take_dirty()) {
    neon_config_apply(g_ui_cfg);
    g_ui_cfg_rev = neon_config_rev();
  }
  if (g_menu.take_action() == neon::MenuModel::Action::kReboot) {
    g_reboot_at_us = now_us + 200000;
  }
}

void service_engine(int64_t now_us) {
  if (timeline_bus().version() != g_timeline_version) {
    g_timeline_version = timeline_bus().read(g_last_snap);
    g_engine.retime(g_last_snap, g_cursor);
    g_have_timeline = true;
  }
  if (engine_config_bus().version() != g_engine_cfg_version) {
    neon::EngineConfig cfg;
    g_engine_cfg_version = engine_config_bus().read(cfg);
    g_engine.set_config(cfg);
    if (g_have_timeline) {
      g_engine.retime(g_last_snap, g_cursor);
    }
  }
  if (!g_have_timeline) {
    return;
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
  neon::TimelineSnapshot tl;
  timeline_bus().read(tl);
  const uint64_t mpb_us = (tl.tempo_mpb_q32 + (1ull << 31)) >> 32;
  s->milli_bpm = mpb_us != 0 ? neon::milli_bpm_from_mpb_us(mpb_us) : 120000;
  s->tempo_valid = tl.tempo_mpb_q32 != 0;
  s->playing = tl.playing != 0;
  s->quantum_beats = tl.quantum_beats != 0 ? tl.quantum_beats : 4;
  s->phase_milli_beats = neon::phase_milli_beats(tl, now_us);
  s->anim_tick =
      static_cast<uint32_t>(now_us / (1000000 / neon::ui::kIconTickHz));
  s->active_net = net::has_ip() ? 1 : 0;  // ethernet or nothing here
  s->peers = app_status_peers();
  s->ext_clock = app_status_ext_clock();
  s->setup_ap = false;
  s->ble_on = false;
  s->big_beat_display = neon_config().big_beat_display != 0;
  net::primary_ip(s->ip, sizeof(s->ip));
}

void service_ui(int64_t now_us) {
  neon::UiStatus status;
  assemble_status(&status, now_us);
  neon::render_ui(g_menu, status, g_fb);
  g_display.draw_ui(g_fb);
  g_display.draw_strip(g_touch.pressed_mask(), status.playing);

  if (neon_config().display_brightness != g_backlight) {
    g_backlight = neon_config().display_brightness;
    g_display.set_backlight(g_backlight);
  }

  digitalWriteFast(kPinLedNet, net::has_ip() ? HIGH : LOW);
  const bool beat_on =
      status.playing && (status.phase_milli_beats % 1000u) < 120u;
  digitalWriteFast(kPinLedBeat, beat_on ? HIGH : LOW);
  digitalWriteFast(kPinLedRun,
                   (PulseHwT41::levels() & (1u << neon::kChRun)) ? HIGH : LOW);
}

}  // namespace

void setup() {
  Serial.begin(115200);
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
  analogWriteFrequency(kPinTempoCv, 20000);  // well above the RC corner

  neon_config_load();
  g_ui_cfg = neon_config();
  g_ui_cfg_rev = neon_config_rev();
  g_backlight = neon_config().display_brightness;

  g_display.init();
  g_display.set_backlight(g_backlight);
  g_display.draw_strip(0, false, /*force=*/true);
  g_touch.init();
  enc::init();
  if (!g_pulse_hw.init()) {
    Serial.println("pulse timer init FAILED");
  }

  net::init(neon_config().device_name);
  audioeng::init();
  miditrs::init();

  const int64_t now = t41_now_us();
  g_cursor = now + kLeadUs;
  linksvc::init(now);
  webui::init();

  char ip[16];
  net::primary_ip(ip, sizeof(ip));
  Serial.printf("net: link=%d ip=%s (editor at http://%s.local/)\n",
                net::link_up() ? 1 : 0, ip[0] != '\0' ? ip : "-",
                neon_config().device_name);
}

void loop() {
  const int64_t now_us = t41_now_us();
  const uint32_t now_ms = millis();

  sync_ui_config();
  service_inputs(now_us, now_ms);

  net::poll(now_us);
  linksvc::poll(now_us);
  webui::poll(now_us);
  miditrs::poll(now_us);
  audioeng::poll(now_us);

  if (static_cast<int32_t>(now_ms - g_next_refill_ms) >= 0) {
    g_next_refill_ms = now_ms + kRefillMs;
    service_engine(now_us);
  }

  if (static_cast<int32_t>(now_ms - g_next_frame_ms) >= 0) {
    g_next_frame_ms = now_ms + kFrameMs;
    service_ui(now_us);
  }

  if (webui::reboot_requested() && g_reboot_at_us == 0) {
    g_reboot_at_us = now_us + 600000;  // let the response drain first
  }
  if (g_reboot_at_us != 0 && now_us >= g_reboot_at_us) {
    do_reboot();
  }
}
