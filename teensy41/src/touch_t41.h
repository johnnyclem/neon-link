#pragma once

#include <cstdint>

// XPT2046 resistive touch, mapped onto the display's two zones:
//
//   strip buttons  +  -  OK  BACK   (DisplayT41::button_rect geometry)
//   UI zone        a tap anywhere acts as OK (encoder click)
//
// Press fires immediately; + and - auto-repeat while held so long lists
// scroll. Raw-to-screen calibration constants are at the top of the .cpp
// — adjust them if your module's corners read differently.
class TouchT41 {
 public:
  enum class Event : uint8_t {
    kNone = 0,
    kPlus,
    kMinus,
    kOk,
    kBack,
  };

  void init();

  // Call every loop iteration; returns one event per call.
  Event poll(uint32_t now_ms);

  // Bitmask of currently held strip buttons, for the display highlight.
  uint8_t pressed_mask() const { return pressed_mask_; }

 private:
  Event classify(int x, int y) const;

  uint8_t pressed_mask_ = 0;
  bool down_ = false;
  Event held_ = Event::kNone;
  uint32_t next_repeat_ms_ = 0;
};
