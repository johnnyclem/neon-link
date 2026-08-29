#pragma once

#include <cstdint>

namespace neon {

// Paint and power policy for the ST7305 reflective panel. Pure logic,
// host-tested; the rlcd task feeds it observations and obeys its
// verdicts. It never talks to the driver.
//
// A reflective LCD is the opposite discipline from the e-paper
// planner: repaints are cheap (a few ms of SPI, no flash, no
// ghosting), but the controller has two power modes — HPM (0x38,
// 16-51 Hz scan, ~hundreds of µA) and LPM (0x39, 0.25-8 Hz scan,
// ~tens of µA) — and switching is a single command byte. So the
// policy is: paint whenever content changes (with small floors so
// ambient churn doesn't spin the SPI bus), hold HPM while anything
// is moving so the scan keeps up with the writes, and drop to LPM
// once the glass has been still for a while. A paint that lands in
// LPM must first hop to HPM or the new frame crawls in at the low
// scan rate.
class RlcdFramePlanner {
 public:
  enum class Power : uint8_t { kHpm, kLpm };

  static constexpr int64_t kUserFloorUs = 33000;      // ~30 fps cap
  static constexpr int64_t kAmbientFloorUs = 100000;  // beat dots stay on time
  static constexpr int64_t kLpmAfterUs = 3000000;     // idle → low power

  // True when the frame should be pushed to the glass now.
  bool plan(int64_t now_us, bool changed, bool user) const {
    if (!changed) {
      return false;
    }
    const int64_t floor_us = user ? kUserFloorUs : kAmbientFloorUs;
    return last_paint_us_ < 0 || now_us - last_paint_us_ >= floor_us;
  }

  void note_painted(int64_t now_us) { last_paint_us_ = now_us; }
  void note_user(int64_t now_us) { last_user_us_ = now_us; }

  // Which scan mode the panel should be in right now.
  Power power(int64_t now_us) const {
    const int64_t last =
        last_paint_us_ > last_user_us_ ? last_paint_us_ : last_user_us_;
    if (last < 0) {
      return Power::kHpm;  // boot: stay fast until the first idle window
    }
    return now_us - last >= kLpmAfterUs ? Power::kLpm : Power::kHpm;
  }

 private:
  int64_t last_paint_us_ = -1;
  int64_t last_user_us_ = -1;
};

}  // namespace neon
