// NEON LINK on a screenless Daisy board (Pod, patch.init()) — the same
// portable engine and polled services as the OLED build (main.cpp),
// minus the menu, framebuffer, and display flush. The board's own
// controls push commands into the control queue and its LEDs carry the
// state; everything else — pulse engine, internal timeline, CLK/RST IN
// follow, TRS MIDI clock, audio engine, QSPI config store — is
// identical (docs/DAISY.md §8).
//
// No network interface on this hardware: the web editor and VST talk to
// a REST surface the ESP32/Teensy targets serve — nothing here can
// carry it, so configuration beyond the panel controls means editing a
// preset on another target and recalling it here, until USB gadget
// networking exists (docs/DAISY.md §7).

#include "daisy_seed.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "neon/multi_engine.hpp"
#include "neon/timeline.hpp"

#include "audio_daisy.h"
#include "board_daisy.h"
#include "board_pins_daisy.h"
#include "clkin_daisy.h"
#include "config_store_daisy.h"
#include "controls_daisy.h"
#include "link_service_daisy.h"
#include "midi_daisy.h"
#include "pulse_hw_daisy.h"
#include "timebase_daisy.h"

namespace {

// Same cadence discipline as the OLED build; no display stall exists
// here, but the QSPI persist path still relies on the pre-persist
// top-up (config_store_daisy.h).
constexpr int64_t kHorizonUs = 60000;
constexpr int64_t kLeadUs = 5000;
constexpr uint32_t kRefillMs = 15;
constexpr uint32_t kLedFrameMs = 33;

neon::MultiClockEngine g_engine;
PulseHwDaisy g_pulse_hw;

int64_t g_cursor = 0;
uint32_t g_timeline_version = 0;
uint32_t g_engine_cfg_version = 0;
bool g_have_timeline = false;
neon::TimelineSnapshot g_last_snap{};
uint32_t g_next_refill_ms = 0;
uint32_t g_next_led_ms = 0;

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

void service_leds(int64_t now_us) {
  neon::TimelineSnapshot tl;
  timeline_bus().read(tl);
  controls::Leds st;
  st.playing = tl.playing != 0;
  st.beat = st.playing &&
            (neon::phase_milli_beats(tl, now_us) % 1000u) < 120u;
  st.run_level = (PulseHwDaisy::levels() & (1u << neon::kChRun)) != 0;
  st.ext_clock = app_status_ext_clock();
  controls::leds(st);
}

}  // namespace

// The 100 µs pulse-timer tick samples the board's encoders (where it
// has any) and CLK/RST IN (declared in pulse_hw_daisy.h).
void neon_daisy_input_sample_isr(int64_t now_us) {
  controls::sample_isr();
  clkin::sample_isr(now_us);
}

int main() {
  board_init();  // SDRAM, QSPI (memory-mapped), codec, clocks
  daisy_time_init();

  neon_config_load();
  neon_daisy_set_pre_persist_hook(persist_topup);

  controls::init();

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

    controls::poll(now_us, now_ms);

    // MIDI note gates: applied immediately at the pin (an edge injected
    // into the ordered ring would wait behind the scheduled stream).
    GateEvent gate;
    while (gate_queue_pop(&gate)) {
      if (gate.channel < neon::kChannelCount) {
        PulseHwDaisy::set_level_now(gate.channel, gate.on);
      }
    }

    linksvc::poll(now_us);
    miditrs::poll(now_us);
    audioeng::poll(now_us);

    if (static_cast<int32_t>(now_ms - g_next_refill_ms) >= 0) {
      g_next_refill_ms = now_ms + kRefillMs;
      service_engine(now_us);
    }

    if (static_cast<int32_t>(now_ms - g_next_led_ms) >= 0) {
      g_next_led_ms = now_ms + kLedFrameMs;
      service_leds(now_us);
    }
  }
}
