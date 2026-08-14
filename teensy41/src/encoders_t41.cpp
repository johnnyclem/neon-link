#include "encoders_t41.h"

#include <Arduino.h>

#include "board_pins_t41.h"
#include "irq_lock_t41.h"
#include "neon/input/quadrature.hpp"

namespace enc {
namespace {

constexpr uint32_t kDebounceMs = 10;
constexpr uint32_t kLongPressMs = 600;

// Set true per encoder if yours counts backwards (A/B swapped on the
// footprint is the usual cause).
constexpr bool kInvert[2] = {false, false};

struct Enc {
  int pin_a, pin_b, pin_sw;
  neon::QuadDecoder decoder;
  volatile int detents = 0;

  // Button state machine (polled).
  bool stable_down = false;
  bool last_raw = false;
  uint32_t raw_since_ms = 0;
  uint32_t down_ms = 0;
  bool long_fired = false;
};

Enc g_enc[2] = {
    {kPinEnc1A, kPinEnc1B, kPinEnc1Sw, {}, 0, false, false, 0, 0, false},
    {kPinEnc2A, kPinEnc2B, kPinEnc2Sw, {}, 0, false, false, 0, 0, false},
};

inline void sample(Enc& e) {
  const unsigned ab = (static_cast<unsigned>(digitalReadFast(e.pin_a)) << 1) |
                      static_cast<unsigned>(digitalReadFast(e.pin_b));
  e.detents += e.decoder.feed(ab);
}

void isr0() { sample(g_enc[0]); }
void isr1() { sample(g_enc[1]); }

}  // namespace

void init() {
  for (Enc& e : g_enc) {
    pinMode(e.pin_a, INPUT_PULLUP);
    pinMode(e.pin_b, INPUT_PULLUP);
    pinMode(e.pin_sw, INPUT_PULLUP);
    const unsigned ab = (static_cast<unsigned>(digitalReadFast(e.pin_a)) << 1) |
                        static_cast<unsigned>(digitalReadFast(e.pin_b));
    e.decoder.reset(ab);
  }
  attachInterrupt(digitalPinToInterrupt(g_enc[0].pin_a), isr0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(g_enc[0].pin_b), isr0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(g_enc[1].pin_a), isr1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(g_enc[1].pin_b), isr1, CHANGE);
}

int take_detents(int index) {
  Enc& e = g_enc[index];
  const uint32_t primask = irq_save();
  const int d = e.detents;
  e.detents = 0;
  irq_restore(primask);
  return kInvert[index] ? -d : d;
}

ButtonEvent poll_button(int index, uint32_t now_ms) {
  Enc& e = g_enc[index];
  const bool raw = digitalReadFast(e.pin_sw) == LOW;  // active low
  if (raw != e.last_raw) {
    e.last_raw = raw;
    e.raw_since_ms = now_ms;
  }
  if (now_ms - e.raw_since_ms < kDebounceMs || raw == e.stable_down) {
    // Still bouncing, or no debounced transition — check for long press.
    if (e.stable_down && !e.long_fired && now_ms - e.down_ms >= kLongPressMs) {
      e.long_fired = true;
      return ButtonEvent::kLongPress;
    }
    return ButtonEvent::kNone;
  }

  e.stable_down = raw;
  if (raw) {
    e.down_ms = now_ms;
    e.long_fired = false;
    return ButtonEvent::kNone;
  }
  return e.long_fired ? ButtonEvent::kNone : ButtonEvent::kClick;
}

}  // namespace enc
