// NEON LINK on an Electrosmith Daisy Seed — internal-timeline build: the
// full pulse engine, 128×64 SSD1306/1309 OLED UI, two rotary encoders,
// TRS MIDI clock, CLK/RST IN external clock following, the audio engine
// on the Seed's built-in codec, Tempo CV on the true DAC, and config +
// presets in QSPI flash.
//
// Same portable core and service shapes as the ESP32-S3 and Teensy 4.1
// firmware — polled services on a bare-metal while(1) loop, with the
// real-time emitters (pulse edges, MIDI clock, audio blocks) in
// interrupts so a slow display push or QSPI erase can never smear an
// output.
//
// Controls:
//   ENC1  rotate = navigate/edit   click = enter/confirm   hold = back
//   ENC2  rotate = tempo ±1 BPM    click = start/stop (quantized)
//
// No network interface on this hardware: Ableton Link, the web editor,
// WiFi, and BLE MIDI live on the other targets (docs/DAISY.md §1). The
// session grid is the InternalTimeline; CLK IN is the only external
// sync, which makes it more central here than anywhere else.

#include "daisy_seed.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "neon/gfx/framebuffer.hpp"
#include "neon/multi_engine.hpp"
#include "neon/transport.hpp"
#include "neon/ui/icons_gen.hpp"
#include "neon/ui/menu_model.hpp"
#include "neon/ui/render.hpp"

#include "audio_daisy.h"
#include "board_daisy.h"
#include "board_pins_daisy.h"
#include "clkin_daisy.h"
#include "config_store_daisy.h"
#include "encoders_daisy.h"
#include "link_service_daisy.h"
#include "midi_daisy.h"
#include "oled_daisy.h"
#include "pulse_hw_daisy.h"
#include "timebase_daisy.h"

namespace {

// Engine scheduling: 15 ms refill with a 60 ms horizon — comfortable
// against the ~1.4 ms SPI display flush. The QSPI config-store erase is
// the one stall the steady-state horizon cannot cover (tens to hundreds
// of ms), so the store's pre-persist hook tops the schedule up first
// (persist_topup below; HANDOFF §6.2).
constexpr int64_t kHorizonUs = 60000;
constexpr int64_t kLeadUs = 5000;
constexpr uint32_t kRefillMs = 15;
constexpr uint32_t kFrameMs = 33;  // ~30 fps UI

neon::Config g_ui_cfg;  // the menu's working copy (synced by config rev)
neon::MenuModel g_menu(&g_ui_cfg);
neon::Framebuffer g_fb;
neon::MultiClockEngine g_engine;
PulseHwDaisy g_pulse_hw;

daisy::GPIO g_led_beat;
daisy::GPIO g_led_run;
daisy::GPIO g_led_ext;

bool g_have_display = false;
int64_t g_cursor = 0;
uint32_t g_timeline_version = 0;
uint32_t g_engine_cfg_version = 0;
bool g_have_timeline = false;
neon::TimelineSnapshot g_last_snap{};
uint32_t g_ui_cfg_rev = ~0u;
uint32_t g_next_refill_ms = 0;
uint32_t g_next_frame_ms = 0;
uint8_t g_brightness = 255;
int64_t g_reboot_at_us = 0;

void do_reboot() {
  neon_config_flush_now();
  HAL_NVIC_SystemReset();
}

// Keep the menu's working copy in step with preset recalls and tempo
// writes from the link service; never yank it mid-edit.
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
  // timeline session keeps its single owner (the link service).
  const int d2 = enc::take_detents(1);
  if (d2 != 0) {
    control_queue_push({ControlCommand::Kind::kNudgeTempo, d2});
  }
  if (enc::poll_button(1, now_ms) == enc::ButtonEvent::kClick) {
    control_queue_push({ControlCommand::Kind::kToggle, 0});
  }

  // MIDI note gates: applied immediately at the pin (an edge injected
  // into the ordered ring would wait behind the scheduled stream).
  GateEvent gate;
  while (gate_queue_pop(&gate)) {
    if (gate.channel < neon::kChannelCount) {
      PulseHwDaisy::set_level_now(gate.channel, gate.on);
    }
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

void service_engine(int64_t now_us, int64_t extra_horizon_us = 0) {
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
  const int64_t until = now_us + kLeadUs + kHorizonUs + extra_horizon_us;
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
        daisy::System::DelayUs(50);
      }
    }
  } while (n == sizeof(edges) / sizeof(edges[0]));
  g_cursor = until;
}

// Registered with the config store: runs right before a blocking QSPI
// erase/program so the pulse ISR has schedule to chew through the stall.
void persist_topup(int64_t extra_horizon_us) {
  service_engine(daisy_now_us(), extra_horizon_us);
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
  s->active_net = 0;  // no network interface on this hardware
  s->peers = app_status_peers();
  s->ext_clock = app_status_ext_clock();
  s->setup_ap = false;
  s->ble_on = false;
  s->big_beat_display = neon_config().big_beat_display != 0;
  s->ip[0] = '\0';
}

void service_ui(int64_t now_us) {
  neon::UiStatus status;
  assemble_status(&status, now_us);

  if (g_have_display) {
    if (neon_config().display_brightness != g_brightness) {
      g_brightness = neon_config().display_brightness;
      oled::set_brightness(g_brightness);
    }
    if (g_brightness != 0) {
      // Native mode draws the design system's compact 128×64 layout;
      // the legacy modes draw the full 128×128 one (oled_daisy.h).
      neon::render_ui(g_menu, status, g_fb, oled::layout());
      oled::flush(g_fb);
    }
  }

  const bool beat_on =
      status.playing && (status.phase_milli_beats % 1000u) < 120u;
  g_led_beat.Write(beat_on);
  g_led_run.Write((PulseHwDaisy::levels() & (1u << neon::kChRun)) != 0);
  g_led_ext.Write(status.ext_clock);
  board_set_led(status.playing);  // onboard LED mirrors the transport
}

}  // namespace

// The 100 µs pulse-timer tick samples the encoders and CLK/RST IN
// (declared in pulse_hw_daisy.h — one 10 kHz heartbeat, no EXTI).
void neon_daisy_input_sample_isr(int64_t now_us) {
  enc::sample_isr();
  clkin::sample_isr(now_us);
}

int main() {
  board_init();  // SDRAM, QSPI (memory-mapped), codec, clocks, CV DAC
  daisy_time_init();

  neon_config_load();
  g_ui_cfg = neon_config();
  g_ui_cfg_rev = neon_config_rev();
  g_brightness = neon_config().display_brightness;
  neon_daisy_set_pre_persist_hook(persist_topup);

  g_led_beat.Init(kPinLedBeat, daisy::GPIO::Mode::OUTPUT);
  g_led_run.Init(kPinLedRun, daisy::GPIO::Mode::OUTPUT);
  g_led_ext.Init(kPinLedExt, daisy::GPIO::Mode::OUTPUT);

  g_have_display = oled::init();
  if (g_have_display) {
    oled::set_brightness(g_brightness);
  }

  enc::init();

  const int64_t now = daisy_now_us();
  g_cursor = now + kLeadUs;
  // linksvc::init configures CLK/RST IN before the pulse timer starts
  // calling the sampling hook.
  linksvc::init(now);
  if (!g_pulse_hw.init()) {
    board_set_led(true);  // pulse timer failed: solid onboard LED
  }
  miditrs::init();
  audioeng::init();

  for (;;) {
    const int64_t now_us = daisy_now_us();
    const uint32_t now_ms = daisy::System::GetNow();

    sync_ui_config();
    service_inputs(now_us, now_ms);

    linksvc::poll(now_us);
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

    if (g_reboot_at_us != 0 && now_us >= g_reboot_at_us) {
      do_reboot();
    }
  }
}
