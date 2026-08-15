// Daisy patch.init() control surface (docs/DAISY.md §8):
//   button (B7)  = quantized start/stop; hold = resync (next loop)
//   toggle (B8)  = clock source: up = auto (follow CLK IN when present),
//                  down = internal only (ignore CLK IN)
//   knob 1 (CV_1)= tempo, soft-pickup: inert until it moves, then an
//                  absolute 20-300 BPM control — so a knob position left
//                  over from the last patch never yanks the tempo at boot
//   panel LED    = beat flash while playing
//
// Everything lands in the control queue / config store, so CLK IN,
// MIDI clock out, presets, and the audio engine behave exactly as on
// the other boards.

#include "controls_daisy.h"

#include <cmath>

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"

#include "board_daisy.h"
#include "board_pins_daisy.h"
#include "buttons_daisy.h"

namespace controls {
namespace {

// Knob-tempo mapping (matches the Tempo CV jack's default span).
constexpr float kKnobMinBpm = 20.0f;
constexpr float kKnobMaxBpm = 300.0f;
// The knob wakes once it travels this far from its boot position.
constexpr float kKnobPickup = 0.02f;

daisy::GPIO g_toggle;
float g_knob_at_boot = -1.0f;
bool g_knob_engaged = false;
uint32_t g_last_knob_mbpm = 0;
bool g_toggle_known = false;
bool g_toggle_last = false;

void poll_knob_tempo() {
  g_patch.ProcessAnalogControls();
  const float k = g_patch.GetAdcValue(daisy::patch_sm::CV_1);
  if (g_knob_at_boot < 0.0f) {
    g_knob_at_boot = k;
    return;
  }
  if (!g_knob_engaged) {
    if (std::fabs(k - g_knob_at_boot) < kKnobPickup) {
      return;
    }
    g_knob_engaged = true;
  }
  const float bpm = kKnobMinBpm + k * (kKnobMaxBpm - kKnobMinBpm);
  // Whole-BPM steps, pushed only on change, so the queue stays quiet
  // and the tempo reads as a round number everywhere.
  const uint32_t mbpm = static_cast<uint32_t>(bpm + 0.5f) * 1000u;
  if (mbpm != g_last_knob_mbpm) {
    g_last_knob_mbpm = mbpm;
    control_queue_push(
        {ControlCommand::Kind::kSetTempo, static_cast<int32_t>(mbpm)});
  }
}

void poll_toggle() {
  // Up (open, pulled high) = auto-follow CLK IN; down = internal only.
  const bool up = g_toggle.Read();
  if (g_toggle_known && up == g_toggle_last) {
    return;
  }
  const neon::ClockSource want =
      up ? neon::ClockSource::kAuto : neon::ClockSource::kLinkMaster;
  g_toggle_known = true;
  g_toggle_last = up;
  if (neon_config().clock_source != want) {
    neon::Config next = neon_config();
    next.clock_source = want;
    neon_config_apply(next);
  }
}

}  // namespace

void init() {
  btn::init();
  g_toggle.Init(kPinToggle, daisy::GPIO::Mode::INPUT,
                daisy::GPIO::Pull::PULLUP);
}

void sample_isr() {}  // no encoders on this hardware

void poll(int64_t now_us, uint32_t now_ms) {
  (void)now_us;
  switch (btn::poll(0, now_ms)) {
    case btn::Event::kClick:
      control_queue_push({ControlCommand::Kind::kToggle, 0});
      break;
    case btn::Event::kLongPress:
      control_queue_push({ControlCommand::Kind::kResyncNextLoop, 0});
      break;
    default:
      break;
  }
  poll_toggle();
  poll_knob_tempo();
}

void leds(const Leds& state) {
  board_set_led(state.playing && state.beat);
}

}  // namespace controls
