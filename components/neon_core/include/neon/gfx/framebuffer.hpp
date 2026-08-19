#pragma once

#include <cstddef>
#include <cstdint>

namespace neon {

// 128×128 monochrome framebuffer in SSD1306/SH1107 page layout (each byte
// is 8 vertical pixels, pages of 8 rows, column-major within a page).
// AMYboard's Grove OLEDs are 128×128 (SSD1327 grayscale / SH1107 mono);
// smaller 128×64 panels can still be driven by using the top half.
// Portable — host tests render screens and compare ASCII dumps.
class Framebuffer {
 public:
  static constexpr int kWidth = 128;
  static constexpr int kHeight = 128;
  static constexpr size_t kSize = kWidth * kHeight / 8;  // 2048

  enum class Font : uint8_t {
    kSmall = 1,   // 5×7
    kMedium = 2,  // 10×14 (2× scale)
    kLarge = 3,   // 15×21 (3× scale)
  };

  // Ordered fills for depth on a 1-bit panel. DESIGN_SYSTEM.md §3.2 keeps
  // the core UI pure black and white; these are for progress and meter
  // fills only, and must never sit behind text.
  enum class Dither : uint8_t {
    kSolid,    // every pixel
    kHalf,     // 50% checkerboard
    kQuarter,  // 25%
  };

  void clear();
  void set_pixel(int x, int y, bool on);
  bool pixel(int x, int y) const;
  void fill_rect(int x, int y, int w, int h, bool on);
  void fill_rect_dither(int x, int y, int w, int h, Dither pattern);
  void rect(int x, int y, int w, int h, bool on);
  void draw_line(int x0, int y0, int x1, int y1, bool on);

  // Flips every pixel in the region. This is how selection and focus are
  // shown on the panel — there is no colour to fall back on.
  void invert_rect(int x, int y, int w, int h);

  // Draws a 1-bit bitmap stored one byte per row, bit 7 = leftmost pixel
  // (the packing scripts/gen_design.py emits for the icon masters).
  void blit(int x, int y, const uint8_t* rows, int w, int h);

  // Draws text with its top-left at (x, y); returns the advance width.
  // Glyphs are 5(+1) columns wide at scale 1.
  int draw_text(int x, int y, const char* s, Font font);
  static int text_width(const char* s, Font font);
  static int glyph_height(Font font);

  const uint8_t* data() const { return buf_; }

  void copy_from(const Framebuffer& other);
  // Count of pixels that differ. Used to decide whether the next flush
  // is a "big redraw" that needs to start before the audible beat.
  int diff_pixels(const Framebuffer& other) const;

  // 30%: a change that rewrites enough of the glass that I2C/SPI
  // scan-out is visible against the downbeat.
  static constexpr int kBigRedrawPercent = 30;
  static bool is_big_redraw(int changed_pixels) {
    return changed_pixels * 100 >= kWidth * kHeight * kBigRedrawPercent;
  }

  // "#"/"." rows for golden tests; out must hold kWidth+1 chars.
  void ascii_row(int y, char* out) const;

 private:
  uint8_t buf_[kSize] = {};
};

}  // namespace neon
