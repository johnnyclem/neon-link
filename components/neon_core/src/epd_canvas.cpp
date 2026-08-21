#include "neon/gfx/epd_canvas.hpp"

#include "neon/gfx/font5x7.hpp"

#include <cstdio>
#include <cstring>

namespace neon {

void EpdCanvas::set_pixel(int x, int y, bool ink) {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
    return;
  }
  uint8_t* p = &buf_[static_cast<size_t>(y) * kStride +
                     static_cast<size_t>(x / 8)];
  const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7));
  if (ink) {
    *p = static_cast<uint8_t>(*p & ~bit);  // black
  } else {
    *p = static_cast<uint8_t>(*p | bit);  // white
  }
}

bool EpdCanvas::pixel(int x, int y) const {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
    return false;
  }
  const uint8_t b = buf_[static_cast<size_t>(y) * kStride +
                         static_cast<size_t>(x / 8)];
  return (b & (0x80u >> (x & 7))) == 0;  // true = black
}

void EpdCanvas::fill_rect(int x, int y, int w, int h, bool ink) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      set_pixel(xx, yy, ink);
    }
  }
}

int EpdCanvas::draw_text(int x, int y, const char* s, int scale) {
  if (s == nullptr || scale < 1) {
    return 0;
  }
  int cx = x;
  for (const char* p = s; *p != '\0'; ++p) {
    const uint8_t* g = gfx::glyph5x7(*p);
    for (int col = 0; col < 5; ++col) {
      const uint8_t bits = g[col];
      for (int row = 0; row < 7; ++row) {
        if ((bits >> row) & 1u) {
          fill_rect(cx + col * scale, y + row * scale, scale, scale, true);
        }
      }
    }
    cx += 6 * scale;
  }
  return cx - x;
}

void EpdCanvas::invert() {
  for (size_t i = 0; i < kSize; ++i) {
    buf_[i] = static_cast<uint8_t>(~buf_[i]);
  }
}

void EpdCanvas::invert_rect(int x, int y, int w, int h) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      set_pixel(xx, yy, !pixel(xx, yy));
    }
  }
}

int EpdCanvas::black_pixels() const {
  int n = 0;
  for (size_t i = 0; i < kSize; ++i) {
    n += __builtin_popcount(static_cast<unsigned>(static_cast<uint8_t>(~buf_[i])));
  }
  return n;
}

void draw_left_arrow(EpdCanvas& c, int cy) {
  const int tip_x = 10;
  const int half = 16;
  for (int i = 0; i <= half; ++i) {
    c.fill_rect(tip_x + i, cy - i, 4, 2 * i + 1, true);
  }
  c.fill_rect(tip_x + half - 2, cy - 5, 36, 10, true);
}

void render_linksync_splash(EpdCanvas& c) {
  c.clear();
  constexpr int kW = EpdCanvas::kWidth;
  constexpr int kH = EpdCanvas::kHeight;
  draw_left_arrow(c, kH / 5);
  draw_left_arrow(c, (kH * 4) / 5);

  const char* mark = "NEON LINK";
  const int mark_scale = 6;
  const int mark_w = static_cast<int>(std::strlen(mark)) * 6 * mark_scale;
  const int mark_x = (kW - mark_w) / 2;
  const int mark_y = 86;
  c.fill_rect(mark_x - 12, mark_y - 16, mark_w + 24, 6, true);
  c.draw_text(mark_x, mark_y, mark, mark_scale);
  c.fill_rect(mark_x - 12, mark_y + 7 * mark_scale + 10, mark_w + 24, 6, true);

  const char* line = "press the 2 side buttons to start";
  const int line_scale = 2;
  const int line_w = static_cast<int>(std::strlen(line)) * 6 * line_scale;
  c.draw_text((kW - line_w) / 2, 176, line, line_scale);
}

void render_linksync_panel(EpdCanvas& c, const LinkSyncPanelStatus& s) {
  if (s.overlay == 4) {
    render_linksync_splash(c);
    return;
  }
  c.clear();
  c.fill_rect(0, 0, EpdCanvas::kWidth, 8, true);
  c.fill_rect(0, EpdCanvas::kHeight - 8, EpdCanvas::kWidth, 8, true);

  c.draw_text(24, 24, s.title[0] != '\0' ? s.title : "link-sync", 3);

  char bpm[24];
  std::snprintf(bpm, sizeof(bpm), "%u.%u",
                static_cast<unsigned>(s.milli_bpm / 1000u),
                static_cast<unsigned>((s.milli_bpm / 100u) % 10u));
  c.draw_text(24, 72, bpm, 6);
  c.draw_text(24 + static_cast<int>(std::strlen(bpm)) * 36 + 16, 104, "BPM",
              3);

  if (s.overlay == 0) {
    c.draw_text(24, 176, s.playing ? "PLAYING" : "STOPPED", 4);
  }

  char peers[24];
  std::snprintf(peers, sizeof(peers), "PEERS %u",
                static_cast<unsigned>(s.peers));
  c.draw_text(400, 24, peers, 3);

  // Physical access is the credential: print the setup AP password
  // on the glass whenever the box is offering that network.
  const bool show_ap =
      (s.setup_ap || !s.provisioned) && s.ap_pass[0] != '\0';
  if (show_ap) {
    c.draw_text(400, 72, s.ap_ssid[0] != '\0' ? s.ap_ssid : "SETUP AP", 2);
    c.draw_text(400, 108, s.ap_pass, 4);
  } else {
    const char* net = !s.provisioned ? "UNPROVISIONED"
                      : s.wifi_up    ? s.ssid
                                     : "CONNECTING";
    c.draw_text(400, 72, net, 2);
  }

  if (s.overlay == 1 || s.overlay == 2) {
    const int row0 = 160;
    const int row_h = 18;
    for (int i = 0; i < s.n_items && i < 8; ++i) {
      const int y = row0 + i * row_h;
      char line[48];
      std::snprintf(line, sizeof(line), "%s  %s", s.item_label[i],
                    s.item_value[i]);
      c.draw_text(24, y, line, 2);
      if (i == s.cursor) {
        c.invert_rect(16, y - 2, 360, row_h);
      }
    }
    if (s.overlay == 2) {
      c.draw_text(400, 232, "EDIT", 3);
    }
  } else if (s.overlay == 3) {
    c.fill_rect(220, 70, 352, 140, true);
    c.fill_rect(224, 74, 344, 132, false);
    static const char* kPower[3] = {"RESTART", "POWER OFF", "CANCEL"};
    for (int i = 0; i < 3; ++i) {
      const int y = 88 + i * 36;
      c.draw_text(260, y, kPower[i], 3);
      if (i == s.power_cursor) {
        c.invert_rect(240, y - 4, 312, 28);
      }
    }
  } else if (s.detail[0] != '\0') {
    c.draw_text(24, 232, s.detail, 2);
  } else {
    c.draw_text(24, 232, "MIDI CLOCK  24 PPQN  TRS-A", 2);
  }

  if (s.invert) {
    c.invert();
  }
}

}  // namespace neon
