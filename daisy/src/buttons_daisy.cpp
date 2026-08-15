#include "buttons_daisy.h"

#include "daisy_seed.h"

#include "board_pins_daisy.h"

namespace btn {
namespace {

constexpr uint32_t kDebounceMs = 10;
constexpr uint32_t kLongPressMs = 600;

struct Button {
  daisy::GPIO gpio;
  bool stable_down = false;
  bool last_raw = false;
  uint32_t raw_since_ms = 0;
  uint32_t down_ms = 0;
  bool long_fired = false;
};

Button g_btn[kNumButtons];

}  // namespace

void init() {
  for (int i = 0; i < kNumButtons; ++i) {
    g_btn[i].gpio.Init(kButtonPins[i], daisy::GPIO::Mode::INPUT,
                       daisy::GPIO::Pull::PULLUP);
  }
}

Event poll(int index, uint32_t now_ms) {
  Button& b = g_btn[index];
  const bool raw = !b.gpio.Read();  // active low
  if (raw != b.last_raw) {
    b.last_raw = raw;
    b.raw_since_ms = now_ms;
  }
  if (now_ms - b.raw_since_ms < kDebounceMs || raw == b.stable_down) {
    if (b.stable_down && !b.long_fired && now_ms - b.down_ms >= kLongPressMs) {
      b.long_fired = true;
      return Event::kLongPress;
    }
    return Event::kNone;
  }

  b.stable_down = raw;
  if (raw) {
    b.down_ms = now_ms;
    b.long_fired = false;
    return Event::kNone;
  }
  return b.long_fired ? Event::kNone : Event::kClick;
}

bool held(int index) { return g_btn[index].stable_down; }

}  // namespace btn
