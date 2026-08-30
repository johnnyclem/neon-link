#pragma once

// Packed 1-bit canvas for the Waveshare 5.79" panel (792×272).
// Row-major, MSB = leftmost pixel, 1 = white, 0 = black (Waveshare).
// Portable so the status layout can be unit-tested on the host.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace neon {

struct LinkSyncPanelStatus {
  char title[24] = "link-sync";
  uint32_t milli_bpm = 120000;
  bool playing = false;
  uint32_t peers = 0;
  bool provisioned = false;
  bool wifi_up = false;
  bool setup_ap = false;
  char ssid[33] = {};
  char ap_ssid[33] = {};
  char ap_pass[65] = {};
  char detail[48] = {};
  // USB-C VBUS present (CrowPanel: CH340 is VBUS-powered). The 4054
  // CHRG pin is unconnected and BAT is not on an ADC, so this is the
  // charging signal — not a fuel gauge.
  bool usb_power = false;
  bool invert = false;
  // 0 live, 1 settings list, 2 editing a value, 3 power popup, 4 splash.
  uint8_t overlay = 0;
  int cursor = 0;
  int n_items = 0;
  char item_label[10][20] = {};
  char item_value[10][16] = {};
  int power_cursor = 0;
};

class EpdCanvas {
 public:
  static constexpr int kWidth = 792;
  static constexpr int kHeight = 272;
  static constexpr int kStride = kWidth / 8;  // 99
  static constexpr size_t kSize = static_cast<size_t>(kStride * kHeight);

  EpdCanvas() { clear(); }

  void clear() { std::memset(buf_, 0xff, sizeof(buf_)); }  // white

  // ink=true draws black.
  void set_pixel(int x, int y, bool ink);
  bool pixel(int x, int y) const;
  void fill_rect(int x, int y, int w, int h, bool ink);

  // 5×7 glyphs scaled by `scale`. Returns advance width.
  int draw_text(int x, int y, const char* s, int scale);

  void invert();
  void invert_rect(int x, int y, int w, int h);

  const uint8_t* data() const { return buf_; }
  uint8_t* data() { return buf_; }

  // Count of black pixels — host tests use this as a cheap "something
  // was drawn" check without dumping 27 KB.
  int black_pixels() const;

 private:
  uint8_t buf_[kSize];
};

void render_linksync_panel(EpdCanvas& c, const LinkSyncPanelStatus& s);

// True when the two canvases differ; [*row0, *row1] is then the
// inclusive band of differing rows, ready for a banded partial
// refresh. Both buffers are EpdCanvas::kSize bytes.
bool epd_dirty_row_span(const uint8_t* prev, const uint8_t* cur, int* row0,
                        int* row1);

}  // namespace neon
