// Daisy Pod control surface (docs/DAISY.md §8):
//   encoder rotate = tempo ±1 BPM      encoder click = quantized start/stop
//   button 1      = tap tempo          button 2      = resync (next loop)
//   LED 1         = beat flash         LED 2         = green run / amber ext
//
// The Pod's two knobs are deliberately unmapped for now (absolute pots
// make poor tempo controls next to a real encoder); they are listed as
// a follow-up.

#include "controls_daisy.h"

#include "daisy_seed.h"
#include "hid/rgb_led.h"

#include "app_state/timeline_bus.h"

#include "board_pins_daisy.h"
#include "buttons_daisy.h"
#include "encoders_daisy.h"

namespace controls {
namespace {

daisy::RgbLed g_led1;
daisy::RgbLed g_led2;

}  // namespace

void init() {
  enc::init();
  btn::init();
  // Same wiring as libDaisy's DaisyPod::InitLeds (common anode, inverted).
  g_led1.Init(kLed1Pins[0], kLed1Pins[1], kLed1Pins[2], true);
  g_led2.Init(kLed2Pins[0], kLed2Pins[1], kLed2Pins[2], true);
}

void sample_isr() { enc::sample_isr(); }

void poll(int64_t now_us, uint32_t now_ms) {
  (void)now_us;

  const int d = enc::take_detents(0);
  if (d != 0) {
    control_queue_push({ControlCommand::Kind::kNudgeTempo, d});
  }
  if (enc::poll_button(0, now_ms) == enc::ButtonEvent::kClick) {
    control_queue_push({ControlCommand::Kind::kToggle, 0});
  }

  if (btn::poll(0, now_ms) == btn::Event::kClick) {
    control_queue_push({ControlCommand::Kind::kTapTempo, 0});
  }
  if (btn::poll(1, now_ms) == btn::Event::kClick) {
    control_queue_push({ControlCommand::Kind::kResyncNextLoop, 0});
  }

  // The RgbLeds dim by software PWM across Update() calls, so they tick
  // at the (fast) poll rate rather than the LED frame rate.
  g_led1.Update();
  g_led2.Update();
}

void leds(const Leds& state) {
  // LED1: white beat flash while playing, dim red heartbeat when stopped.
  if (state.playing) {
    const float v = state.beat ? 1.0f : 0.0f;
    g_led1.Set(v, v, v);
  } else {
    g_led1.Set(0.1f, 0.0f, 0.0f);
  }
  // LED2: run gate in green; amber overlay while locked to CLK IN.
  const float run = state.run_level ? 1.0f : 0.0f;
  g_led2.Set(state.ext_clock ? 0.8f : 0.0f, run, 0.0f);
}

}  // namespace controls
