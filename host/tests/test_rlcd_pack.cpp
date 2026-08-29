#include <doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "neon/gfx/rlcd_canvas.hpp"
#include "neon/gfx/rlcd_pack.hpp"

namespace {

using neon::RlcdCanvas;
namespace rlcd = neon::rlcd;

// Reference implementation: the vendor's two-stage path for this exact
// glass (SolarOS rlcd_st7305.c), transcribed byte for byte. Stage 1 is
// present_mono_xbm's landscape → u8g2-buffer rotation; stage 2 is
// rlcd_pack_tile_window's 12-column/3-byte controller packing with the
// st_lut. If neon::rlcd::pack_frame ever disagrees with this, the
// picture on the hardware is wrong.
constexpr int kTileWidth = 38;               // RLCD_TILE_WIDTH
constexpr int kBufRowBytes = kTileWidth * 8; // 304
constexpr int kTiles = 50;                   // RLCD_TILE_HEIGHT

// Stage 1: landscape canvas (1 = white) into the u8g2 staging buffer.
// present_mono_xbm writes background 0xFF and clears ink bits, so a
// buffer bit is 1 iff the landscape pixel is white. Buffer layout:
// buf[(x / 8) * 304 + (299 - y)], bit index x % 8 (LSB = lowest x).
std::vector<uint8_t> reference_stage1(const RlcdCanvas& c) {
  std::vector<uint8_t> buf(static_cast<size_t>(kBufRowBytes) * kTiles, 0xff);
  for (int y = 0; y < RlcdCanvas::kHeight; ++y) {
    for (int x = 0; x < RlcdCanvas::kWidth; ++x) {
      if (c.pixel(x, y)) {  // black ink
        const size_t idx = static_cast<size_t>(x / 8) * kBufRowBytes +
                           static_cast<size_t>(299 - y);
        buf[idx] = static_cast<uint8_t>(buf[idx] & ~(1u << (x % 8)));
      }
    }
  }
  return buf;
}

// Stage 2: rlcd_pack_tile_window for the full window of one tile.
void reference_pack_tile(const uint8_t* row_base, uint8_t* rows) {
  static const uint8_t st_lut[4][4] = {
      {0x00, 0x80, 0x40, 0xC0},
      {0x00, 0x20, 0x10, 0x30},
      {0x00, 0x08, 0x04, 0x0C},
      {0x00, 0x02, 0x01, 0x03},
  };
  std::memset(rows, 0, static_cast<size_t>(rlcd::kPackedRowBytes) * 4);
  for (int source_row = 0; source_row < 4; ++source_row) {
    const int shift = source_row * 2;
    int index = source_row * rlcd::kPackedRowBytes;
    for (int col = 0; col + 3 < rlcd::kNativeCols; col += 4, ++index) {
      rows[index] =
          static_cast<uint8_t>(st_lut[0][(row_base[col] >> shift) & 3] |
                               st_lut[1][(row_base[col + 1] >> shift) & 3] |
                               st_lut[2][(row_base[col + 2] >> shift) & 3] |
                               st_lut[3][(row_base[col + 3] >> shift) & 3]);
    }
  }
}

std::vector<uint8_t> reference_pack(const RlcdCanvas& c) {
  const std::vector<uint8_t> buf = reference_stage1(c);
  std::vector<uint8_t> out(rlcd::kPackedSize, 0);
  for (int tile = 0; tile < kTiles; ++tile) {
    reference_pack_tile(buf.data() + static_cast<size_t>(tile) * kBufRowBytes,
                        out.data() + static_cast<size_t>(tile) * 4 *
                                         rlcd::kPackedRowBytes);
  }
  return out;
}

}  // namespace

TEST_CASE("packed geometry matches the ST7305 window") {
  CHECK(rlcd::kColumnGroups == 25);
  CHECK(rlcd::kPackedRowBytes == 75);
  CHECK(rlcd::kPackedRows == 200);
  CHECK(rlcd::kPackedSize == 15000);
  CHECK(rlcd::kCasetLo == 0x12);
  CHECK(rlcd::kCasetHi == 0x2A);
}

TEST_CASE("all-white and all-black canvases pack to solid planes") {
  RlcdCanvas c;
  std::vector<uint8_t> out(rlcd::kPackedSize, 0);
  rlcd::pack_frame(c.data(), out.data());
  for (uint8_t b : out) {
    CHECK(b == 0xff);
  }
  c.fill_rect(0, 0, RlcdCanvas::kWidth, RlcdCanvas::kHeight, true);
  rlcd::pack_frame(c.data(), out.data());
  for (uint8_t b : out) {
    CHECK(b == 0x00);
  }
}

TEST_CASE("single pixels land where the vendor path puts them") {
  static const int px[][2] = {{0, 0},   {399, 0},   {0, 299}, {399, 299},
                              {1, 0},   {0, 1},     {200, 150}, {7, 7},
                              {8, 12},  {395, 293}};
  for (const auto& p : px) {
    RlcdCanvas c;
    c.set_pixel(p[0], p[1], true);
    std::vector<uint8_t> out(rlcd::kPackedSize, 0);
    rlcd::pack_frame(c.data(), out.data());
    const std::vector<uint8_t> ref = reference_pack(c);
    CHECK(std::memcmp(out.data(), ref.data(), rlcd::kPackedSize) == 0);
    // Exactly one bit of ink in the whole packed frame.
    int dark = 0;
    for (uint8_t b : out) {
      dark += __builtin_popcount(0xffu & ~b);
    }
    CHECK(dark == 1);
  }
}

TEST_CASE("a rendered panel packs identically to the vendor path") {
  RlcdCanvas c;
  neon::RlcdPanelStatus s;
  s.base.milli_bpm = 128300;
  s.base.playing = true;
  s.base.peers = 2;
  s.base.provisioned = true;
  s.base.wifi_up = true;
  s.beat = 3;
  s.quantum = 4;
  s.battery_pct = 67;
  neon::render_rlcd_panel(c, s);
  CHECK(c.black_pixels() > 500);

  std::vector<uint8_t> out(rlcd::kPackedSize, 0);
  rlcd::pack_frame(c.data(), out.data());
  const std::vector<uint8_t> ref = reference_pack(c);
  CHECK(std::memcmp(out.data(), ref.data(), rlcd::kPackedSize) == 0);
}

TEST_CASE("pack_rows packs a strip exactly like the full frame") {
  RlcdCanvas c;
  neon::RlcdPanelStatus s;
  s.base.milli_bpm = 174000;
  neon::render_rlcd_panel(c, s);

  std::vector<uint8_t> full(rlcd::kPackedSize, 0);
  rlcd::pack_frame(c.data(), full.data());

  const int row0 = 37;
  const int count = 61;
  std::vector<uint8_t> strip(static_cast<size_t>(count) *
                             rlcd::kPackedRowBytes);
  rlcd::pack_rows(c.data(), row0, count, strip.data());
  CHECK(std::memcmp(strip.data(),
                    full.data() +
                        static_cast<size_t>(row0) * rlcd::kPackedRowBytes,
                    strip.size()) == 0);
}
