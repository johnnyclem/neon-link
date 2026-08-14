#include "touch_t41.h"

#include <Arduino.h>
#include <XPT2046_Touchscreen.h>

#include "board_pins_t41.h"
#include "display_t41.h"

namespace {

// Raw ADC extents of the resistive film. Typical for the PJRC 2.8"
// module; recalibrate by logging raw points at the corners.
constexpr int kRawXMin = 340;
constexpr int kRawXMax = 3800;
constexpr int kRawYMin = 240;
constexpr int kRawYMax = 3850;

constexpr uint32_t kRepeatDelayMs = 450;
constexpr uint32_t kRepeatRateMs = 140;

XPT2046_Touchscreen g_ts(kPinTouchCs, kPinTouchIrq);

int scale(int v, int lo, int hi, int out_max) {
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return static_cast<int>(static_cast<int32_t>(v - lo) * out_max / (hi - lo));
}

}  // namespace

void TouchT41::init() {
  g_ts.begin();
  // Match the display: rotation 1, origin top-left of the landscape panel.
  g_ts.setRotation(1);
}

TouchT41::Event TouchT41::classify(int x, int y) const {
  if (x < DisplayT41::kStripX) {
    return Event::kOk;  // tap on the UI = encoder click
  }
  for (int i = 0; i < kBtnCount; ++i) {
    int bx, by, bw, bh;
    DisplayT41::button_rect(i, &bx, &by, &bw, &bh);
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
      switch (i) {
        case kBtnPlus: return Event::kPlus;
        case kBtnMinus: return Event::kMinus;
        case kBtnOk: return Event::kOk;
        case kBtnBack: return Event::kBack;
      }
    }
  }
  return Event::kNone;
}

TouchT41::Event TouchT41::poll(uint32_t now_ms) {
  const bool touched = g_ts.touched();
  if (!touched) {
    down_ = false;
    held_ = Event::kNone;
    pressed_mask_ = 0;
    return Event::kNone;
  }

  const TS_Point p = g_ts.getPoint();
  const int x = scale(p.x, kRawXMin, kRawXMax, 319);
  const int y = scale(p.y, kRawYMin, kRawYMax, 239);

  if (!down_) {
    down_ = true;
    held_ = classify(x, y);
    next_repeat_ms_ = now_ms + kRepeatDelayMs;
    pressed_mask_ = 0;
    switch (held_) {
      case Event::kPlus: pressed_mask_ = 1u << kBtnPlus; break;
      case Event::kMinus: pressed_mask_ = 1u << kBtnMinus; break;
      case Event::kOk:
        if (x >= DisplayT41::kStripX) pressed_mask_ = 1u << kBtnOk;
        break;
      case Event::kBack: pressed_mask_ = 1u << kBtnBack; break;
      default: break;
    }
    return held_;
  }

  // Held: auto-repeat the rotate buttons only.
  if ((held_ == Event::kPlus || held_ == Event::kMinus) &&
      static_cast<int32_t>(now_ms - next_repeat_ms_) >= 0) {
    next_repeat_ms_ = now_ms + kRepeatRateMs;
    return held_;
  }
  return Event::kNone;
}
