#pragma once

#include <cstdint>

#include "neon/gfx/framebuffer.hpp"

// 2.8" ILI9341 (320x240 landscape) presentation of the 128x128 firmware
// UI. The panel is split into two zones:
//
//   0..239   the device UI: render_ui()'s mono framebuffer upscaled
//            x1.875 (nearest-neighbour LUT) and colourised with the
//            design tokens — neon cyan on near-black (design/tokens.json)
//   240..319 an 80 px touch strip: four buttons (+ / - / OK / BACK) so
//            the whole menu tree is drivable by touch alone
//
// Every screen keeps being drawn by the portable render_ui(), so the
// TFT can never disagree with the OLED targets or the web style guide.

// Touch-strip button indices (top to bottom).
enum StripButton : uint8_t {
  kBtnPlus = 0,
  kBtnMinus = 1,
  kBtnOk = 2,
  kBtnBack = 3,
  kBtnCount = 4,
};

class DisplayT41 {
 public:
  static constexpr int kUiSize = 240;    // upscaled UI square
  static constexpr int kStripX = 240;    // touch strip left edge
  static constexpr int kStripW = 80;

  // Button geometry, shared with the touch layer's hit testing.
  static void button_rect(int index, int* x, int* y, int* w, int* h);

  void init();

  // Pushes the UI framebuffer if it changed since the last call.
  void draw_ui(const neon::Framebuffer& fb, bool force = false);

  // Redraws the strip when the pressed state (bitmask of StripButton)
  // or the transport word changes.
  void draw_strip(uint8_t pressed_mask, bool playing, bool force = false);

  // 0 blanks the panel, 255 is full brightness (Config.display_brightness).
  void set_backlight(uint8_t level);
};
