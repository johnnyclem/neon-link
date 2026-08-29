#pragma once

// Packed 1-bit canvas for the Waveshare ESP32-S3-RLCD-4.2 reflective
// panel (ST7305, 400×300 landscape). Row-major, MSB = leftmost pixel,
// 1 = white (reflective), 0 = black ink — the same polarity as
// EpdCanvas, so the render code reads identically. Portable so the
// status layout can be unit-tested on the host.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "neon/gfx/epd_canvas.hpp"  // LinkSyncPanelStatus

namespace neon {

// The RLCD face shares the link-sync view model and adds what the
// board can actually show: a live beat, a battery gauge, and the
// panel's power mode. All fields portable and host-tested.
struct RlcdPanelStatus {
  LinkSyncPanelStatus base;
  // 1-based beat within the quantum while playing; 0 when stopped.
  uint32_t beat = 0;
  uint32_t quantum = 4;
  // Battery voltage in millivolts after the ×3 divider; 0 = unknown.
  uint32_t battery_mv = 0;
  // 0..100 derived from battery_mv; -1 = unknown.
  int battery_pct = -1;
  // The ST7305 is sitting in low-power (≤8 Hz) refresh.
  bool low_power = false;
};

class RlcdCanvas {
 public:
  static constexpr int kWidth = 400;
  static constexpr int kHeight = 300;
  static constexpr int kStride = kWidth / 8;  // 50
  static constexpr size_t kSize = static_cast<size_t>(kStride * kHeight);

  RlcdCanvas() { clear(); }

  void clear() { std::memset(buf_, 0xff, sizeof(buf_)); }  // white

  // ink=true draws black.
  void set_pixel(int x, int y, bool ink);
  bool pixel(int x, int y) const;
  void fill_rect(int x, int y, int w, int h, bool ink);
  void draw_rect(int x, int y, int w, int h, int t, bool ink);

  // 5×7 glyphs scaled by `scale`. Returns advance width.
  int draw_text(int x, int y, const char* s, int scale);

  void invert();
  void invert_rect(int x, int y, int w, int h);

  const uint8_t* data() const { return buf_; }
  uint8_t* data() { return buf_; }

  int black_pixels() const;

 private:
  uint8_t buf_[kSize];
};

void render_rlcd_splash(RlcdCanvas& c);
void render_rlcd_panel(RlcdCanvas& c, const RlcdPanelStatus& s);

}  // namespace neon
