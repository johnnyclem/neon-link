#include "encoders_daisy.h"

#include "daisy_seed.h"

#include "board_pins_daisy.h"
#include "irq_lock_daisy.h"
#include "neon/input/quadrature.hpp"

namespace enc {
namespace {

constexpr uint32_t kDebounceMs = 10;
constexpr uint32_t kLongPressMs = 600;

// Set true per encoder if yours counts backwards (A/B swapped on the
// footprint is the usual cause).
constexpr bool kInvert[2] = {false, false};

struct Enc {
  daisy::GPIO a, b, sw;
  neon::QuadDecoder decoder;
  volatile int detents = 0;

  // Button state machine (polled from the main loop).
  bool stable_down = false;
  bool last_raw = false;
  uint32_t raw_since_ms = 0;
  uint32_t down_ms = 0;
  bool long_fired = false;
};

Enc g_enc[2];

inline void sample(Enc& e) {
  const unsigned ab = (static_cast<unsigned>(e.a.Read()) << 1) |
                      static_cast<unsigned>(e.b.Read());
  e.detents += e.decoder.feed(ab);
}

}  // namespace

void init() {
  const daisy::Pin pins[2][3] = {
      {kPinEnc1A, kPinEnc1B, kPinEnc1Sw},
      {kPinEnc2A, kPinEnc2B, kPinEnc2Sw},
  };
  for (int i = 0; i < 2; ++i) {
    Enc& e = g_enc[i];
    e.a.Init(pins[i][0], daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
    e.b.Init(pins[i][1], daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
    e.sw.Init(pins[i][2], daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
    const unsigned ab = (static_cast<unsigned>(e.a.Read()) << 1) |
                        static_cast<unsigned>(e.b.Read());
    e.decoder.reset(ab);
  }
}

void sample_isr() {
  sample(g_enc[0]);
  sample(g_enc[1]);
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
  const bool raw = !e.sw.Read();  // active low
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
