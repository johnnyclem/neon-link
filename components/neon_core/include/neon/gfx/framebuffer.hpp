#pragma once

#include <cstddef>
#include <cstdint>

namespace neon {

// 128×64 monochrome framebuffer in SSD1306 page layout (each byte is 8
// vertical pixels, pages of 8 rows, column-major within a page) so the
// device flush is a straight buffer hand-off. Portable — host tests render
// screens and compare ASCII dumps.
class Framebuffer {
 public:
  static constexpr int kWidth = 128;
  static constexpr int kHeight = 64;
  static constexpr size_t kSize = kWidth * kHeight / 8;

  enum class Font : uint8_t {
    kSmall = 1,   // 5×7
    kMedium = 2,  // 10×14 (2× scale)
    kLarge = 3,   // 15×21 (3× scale)
  };

  void clear();
  void set_pixel(int x, int y, bool on);
  bool pixel(int x, int y) const;
  void fill_rect(int x, int y, int w, int h, bool on);
  void rect(int x, int y, int w, int h, bool on);

  // Draws text with its top-left at (x, y); returns the advance width.
  // Glyphs are 5(+1) columns wide at scale 1.
  int draw_text(int x, int y, const char* s, Font font);
  static int text_width(const char* s, Font font);
  static int glyph_height(Font font);

  const uint8_t* data() const { return buf_; }

  // "#"/"." rows for golden tests; out must hold kWidth+1 chars.
  void ascii_row(int y, char* out) const;

 private:
  uint8_t buf_[kSize] = {};
};

}  // namespace neon
