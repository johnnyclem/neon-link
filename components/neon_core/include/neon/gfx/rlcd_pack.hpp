#pragma once

// ST7305 frame packing for the Waveshare ESP32-S3-RLCD-4.2 (400×300
// landscape UI on a 300×400 portrait controller). Pure logic, host
// cross-checked against the vendor's two-stage u8g2 path, so the ESP
// driver only has to move bytes.
//
// Controller geometry: the ST7305 addresses 12 native columns per
// 3-byte group and 2 native rows per byte. One data byte carries
// 4 native columns × 2 native rows, MSB first:
//   bit7 col+0/row+0  bit6 col+0/row+1  bit5 col+1/row+0 ... bit0 col+3/row+1
// Column addresses start at 0x12 and CASET is written mirrored
// (0x3C - addr). Rotation: landscape x runs along the 400 native
// rows, landscape y runs backwards along the 300 native columns —
// native_col = 299 - y, native_row = x — matching the vendor's
// U8G2_R1 orientation on this glass.
//
// Bit sense: 1 = reflective/white, matching RlcdCanvas, valid while
// the driver keeps the panel in INVON (0x21) like the vendor firmware.

#include <cstddef>
#include <cstdint>

namespace neon {
namespace rlcd {

constexpr int kWidth = 400;   // landscape, == RlcdCanvas::kWidth
constexpr int kHeight = 300;  // landscape, == RlcdCanvas::kHeight
constexpr int kCanvasStride = kWidth / 8;

constexpr int kNativeCols = 300;
constexpr int kNativeRows = 400;
constexpr int kColumnGroups = (kNativeCols + 11) / 12;        // 25
constexpr int kPackedRowBytes = kColumnGroups * 3;            // 75
constexpr int kPackedRows = kNativeRows / 2;                  // 200
constexpr size_t kPackedSize =
    static_cast<size_t>(kPackedRowBytes) * kPackedRows;       // 15000

constexpr uint8_t kAddrStart = 0x12;
constexpr uint8_t kAddrEnd = kAddrStart + kColumnGroups - 1;  // 0x2A
// CASET data bytes for the full window, pre-mirrored.
constexpr uint8_t kCasetLo = 0x3C - kAddrEnd;                 // 0x12
constexpr uint8_t kCasetHi = 0x3C - kAddrStart;               // 0x2A

// Packs `count` controller rows starting at `row0` from a full
// landscape canvas (RlcdCanvas layout: row-major, MSB = leftmost,
// 1 = white) into `out` (count * kPackedRowBytes bytes). Controller
// row r covers landscape columns x = 2r and 2r+1, so a narrow row
// span is a narrow vertical strip of the landscape UI.
void pack_rows(const uint8_t* canvas, int row0, int count, uint8_t* out);

// The whole frame: kPackedRows rows into kPackedSize bytes.
inline void pack_frame(const uint8_t* canvas, uint8_t* out) {
  pack_rows(canvas, 0, kPackedRows, out);
}

}  // namespace rlcd
}  // namespace neon
