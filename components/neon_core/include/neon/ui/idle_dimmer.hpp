#pragma once

#include <cstdint>

namespace neon {
namespace ui {

// Idle display-power policy shared by every panel target. Pure logic,
// host-tested; each display service feeds it input edges and maps its
// verdicts onto whatever the hardware has (backlight PWM, OLED
// contrast, render pacing). It never talks to a driver.
//
// Levels: kActive (user brightness, native frame rate), kDim (clamped
// brightness, relaxed frame rate), kBlank (panel dark, rendering
// skipped). A playing transport never blanks — a running tempo box
// must stay glanceable — it only dims. dim_after_s == 0 disables the
// whole feature, which is the shipped default: dimming is opt-in.
class IdleDimmer {
 public:
  enum class Level : uint8_t { kActive, kDim, kBlank };

  // Blank after this many dim windows (stopped transport only).
  static constexpr int kBlankMultiplier = 3;
  static constexpr int kDimFrameMs = 250;
  // Blank polls stay at 2 Hz so the waking touch/press feels instant.
  static constexpr int kBlankFrameMs = 500;

  void configure(uint16_t dim_after_s, uint8_t dim_level) {
    dim_after_us_ = static_cast<int64_t>(dim_after_s) * 1000000;
    dim_level_ = dim_level;
  }

  void note_activity(int64_t now_us) { last_activity_us_ = now_us; }
  void set_playing(bool playing) { playing_ = playing; }

  Level level(int64_t now_us) const {
    if (dim_after_us_ <= 0) {
      return Level::kActive;
    }
    const int64_t idle = now_us - last_activity_us_;
    if (idle < dim_after_us_) {
      return Level::kActive;
    }
    if (playing_ || idle < kBlankMultiplier * dim_after_us_) {
      return Level::kDim;
    }
    return Level::kBlank;
  }

  // Effective brightness for the panel, given the user's set point
  // (0..255 in every target's config).
  uint8_t apply(uint8_t user_brightness, int64_t now_us) const {
    switch (level(now_us)) {
      case Level::kActive:
        return user_brightness;
      case Level::kDim:
        return user_brightness < dim_level_ ? user_brightness : dim_level_;
      case Level::kBlank:
        return 0;
    }
    return user_brightness;
  }

  // Extra frame pacing for render loops: 0 = keep the native rate.
  // While playing the rate stays native even when dim, so beat
  // animation does not stutter under a dimmed backlight.
  int frame_interval_hint_ms(int64_t now_us) const {
    switch (level(now_us)) {
      case Level::kActive:
        return 0;
      case Level::kDim:
        return playing_ ? 0 : kDimFrameMs;
      case Level::kBlank:
        return kBlankFrameMs;
    }
    return 0;
  }

 private:
  int64_t dim_after_us_ = 0;
  int64_t last_activity_us_ = 0;
  uint8_t dim_level_ = 64;
  bool playing_ = false;
};

}  // namespace ui
}  // namespace neon
