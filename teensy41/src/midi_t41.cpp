#include "midi_t41.h"

#include <Arduino.h>

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "neon/midi/midi_encoder.hpp"

#include "irq_lock_t41.h"
#include "timebase_t41.h"

namespace miditrs {
namespace {

IntervalTimer g_timer;

// Staged for the ISR (written IRQ-masked from poll(); a seqlock read
// from the ISR could livelock against a mid-publish main thread on this
// single core).
neon::TimelineSnapshot g_tl{};
volatile int64_t g_next_tick_us = 0;
volatile bool g_clock_on = false;
volatile int32_t g_nudge_us = 0;

// Next 24 PPQN tick strictly after now, on the nudged grid — the same
// solve as the ESP midi service.
bool next_clock_tick_us(const neon::TimelineSnapshot& tl, int64_t now_us,
                        int64_t* out) {
  if (tl.tempo_mpb_q32 == 0) {
    return false;
  }
  const int64_t nudge = g_nudge_us;
  const double mpb = static_cast<double>(tl.tempo_mpb_q32) / 4294967296.0;
  const double grid_now = static_cast<double>(now_us - nudge);
  const double b0 = static_cast<double>(tl.beat_at_origin_q32) / 4294967296.0;
  const double beat =
      b0 + (grid_now - static_cast<double>(tl.origin_us)) / mpb;
  const double tick_beats = 1.0 / 24.0;
  const int64_t tick = static_cast<int64_t>(beat / tick_beats) + 1;
  const double target_beat = static_cast<double>(tick) * tick_beats;
  *out = tl.origin_us + nudge +
         static_cast<int64_t>((target_beat - b0) * mpb);
  if (*out <= now_us) {
    *out = now_us + 1000;
  }
  return true;
}

void tick_isr() {
  if (!g_clock_on) {
    return;
  }
  const int64_t now = t41_now_us();
  if (g_next_tick_us != 0 && now >= g_next_tick_us) {
    Serial1.write(neon::midi::kClock);
  } else if (g_next_tick_us != 0) {
    return;
  }
  int64_t next = 0;
  g_next_tick_us = next_clock_tick_us(g_tl, now, &next) ? next : 0;
}

}  // namespace

void init() {
  Serial1.begin(31250);
  g_timer.priority(96);  // below the pulse emitter
  g_timer.begin(tick_isr, 500);
}

void poll(int64_t now_us) {
  (void)now_us;
  const neon::Config& cfg = neon_config();
  const bool enabled =
      cfg.midi_clock_out != 0 &&
      cfg.midi.clock_policy != neon::MidiRouteConfig::ClockPolicy::kReplace;

  neon::TimelineSnapshot tl;
  timeline_bus().read(tl);

  {
    const uint32_t primask = irq_save();
    g_tl = tl;
    g_nudge_us = cfg.midi_nudge_us;
    g_clock_on = enabled;
    irq_restore(primask);
  }

  // Transport bytes follow the session.
  static bool last_playing = false;
  const bool playing = tl.playing != 0;
  if (playing != last_playing) {
    if (enabled) {
      Serial1.write(playing ? neon::midi::kStart : neon::midi::kStop);
    }
    last_playing = playing;
  }
}

}  // namespace miditrs
