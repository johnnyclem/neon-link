#include "neon/gfx/rlcd_pack.hpp"

#include <cstring>

namespace neon {
namespace rlcd {

namespace {

// Canvas bit at landscape (x, y): 1 = white. Out-of-range y (the two
// padding columns of the last 12-column group, native cols ≥ 300 map
// to y < 0) reads as white.
inline int canvas_bit(const uint8_t* canvas, int x, int y) {
  if (y < 0 || y >= kHeight) {
    return 1;
  }
  const uint8_t b = canvas[static_cast<size_t>(y) * kCanvasStride +
                           static_cast<size_t>(x >> 3)];
  return (b >> (7 - (x & 7))) & 1;
}

}  // namespace

void pack_rows(const uint8_t* canvas, int row0, int count, uint8_t* out) {
  if (canvas == nullptr || out == nullptr || row0 < 0 || count <= 0 ||
      row0 + count > kPackedRows) {
    return;
  }
  for (int r = row0; r < row0 + count; ++r) {
    const int x0 = 2 * r;      // landscape column for the row's even bit
    const int x1 = 2 * r + 1;  // and for the odd bit
    uint8_t* dst = out + static_cast<size_t>(r - row0) * kPackedRowBytes;
    for (int g = 0; g < kColumnGroups; ++g) {
      for (int b = 0; b < 3; ++b) {
        const int col = g * 12 + b * 4;  // native column of bit7/bit6
        uint8_t v = 0;
        for (int k = 0; k < 4; ++k) {
          const int y = kHeight - 1 - (col + k);  // native_col -> landscape y
          v = static_cast<uint8_t>(
              v | (canvas_bit(canvas, x0, y) << (7 - 2 * k)) |
              (canvas_bit(canvas, x1, y) << (6 - 2 * k)));
        }
        *dst++ = v;
      }
    }
  }
}

}  // namespace rlcd
}  // namespace neon
