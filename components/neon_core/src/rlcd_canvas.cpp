#include "neon/gfx/rlcd_canvas.hpp"

#include "neon/gfx/font5x7.hpp"

#include <cstdio>
#include <cstring>

namespace neon {

void RlcdCanvas::set_pixel(int x, int y, bool ink) {
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

bool RlcdCanvas::pixel(int x, int y) const {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
    return false;
  }
  const uint8_t b = buf_[static_cast<size_t>(y) * kStride +
                         static_cast<size_t>(x / 8)];
  return (b & (0x80u >> (x & 7))) == 0;  // true = black
}

void RlcdCanvas::fill_rect(int x, int y, int w, int h, bool ink) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      set_pixel(xx, yy, ink);
    }
  }
}

void RlcdCanvas::draw_rect(int x, int y, int w, int h, int t, bool ink) {
  fill_rect(x, y, w, t, ink);
  fill_rect(x, y + h - t, w, t, ink);
  fill_rect(x, y, t, h, ink);
  fill_rect(x + w - t, y, t, h, ink);
}

int RlcdCanvas::draw_text(int x, int y, const char* s, int scale) {
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

void RlcdCanvas::invert() {
  for (size_t i = 0; i < kSize; ++i) {
    buf_[i] = static_cast<uint8_t>(~buf_[i]);
  }
}

void RlcdCanvas::invert_rect(int x, int y, int w, int h) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      set_pixel(xx, yy, !pixel(xx, yy));
    }
  }
}

int RlcdCanvas::black_pixels() const {
  int n = 0;
  for (size_t i = 0; i < kSize; ++i) {
    n += __builtin_popcount(
        static_cast<unsigned>(static_cast<uint8_t>(~buf_[i])));
  }
  return n;
}

namespace {

// Chunky battery outline with a proportional fill. pct < 0 draws the
// outline with a "?" instead of a level.
void draw_battery(RlcdCanvas& c, int x, int y, int pct) {
  c.draw_rect(x, y, 34, 16, 2, true);
  c.fill_rect(x + 34, y + 4, 4, 8, true);
  if (pct < 0) {
    c.draw_text(x + 12, y + 4, "?", 1);
    return;
  }
  const int fill = (pct > 100 ? 100 : pct) * 28 / 100;
  if (fill > 0) {
    c.fill_rect(x + 3, y + 3, fill, 10, true);
  }
}

void draw_left_arrow(RlcdCanvas& c, int cy) {
  const int tip_x = 8;
  const int half = 12;
  for (int i = 0; i <= half; ++i) {
    c.fill_rect(tip_x + i, cy - i, 3, 2 * i + 1, true);
  }
  c.fill_rect(tip_x + half - 2, cy - 4, 26, 8, true);
}

}  // namespace

void render_rlcd_splash(RlcdCanvas& c) {
  c.clear();
  constexpr int kW = RlcdCanvas::kWidth;
  constexpr int kH = RlcdCanvas::kHeight;
  draw_left_arrow(c, kH / 5);
  draw_left_arrow(c, (kH * 4) / 5);

  const char* mark = "NEON LINK";
  const int mark_scale = 6;
  const int mark_w = static_cast<int>(std::strlen(mark)) * 6 * mark_scale;
  const int mark_x = (kW - mark_w) / 2;
  const int mark_y = 108;
  c.fill_rect(mark_x - 10, mark_y - 14, mark_w + 20, 5, true);
  c.draw_text(mark_x, mark_y, mark, mark_scale);
  c.fill_rect(mark_x - 10, mark_y + 7 * mark_scale + 9, mark_w + 20, 5, true);

  const char* line = "press a button to start";
  const int line_scale = 2;
  const int line_w = static_cast<int>(std::strlen(line)) * 6 * line_scale;
  c.draw_text((kW - line_w) / 2, 208, line, line_scale);
}

void render_rlcd_panel(RlcdCanvas& c, const RlcdPanelStatus& rs) {
  const LinkSyncPanelStatus& s = rs.base;
  if (s.overlay == 4) {
    render_rlcd_splash(c);
    return;
  }
  c.clear();
  constexpr int kW = RlcdCanvas::kWidth;

  // Header: name left, peers + battery right, rule underneath.
  c.draw_text(12, 10, s.title[0] != '\0' ? s.title : "link-rlcd", 2);
  char peers[16];
  std::snprintf(peers, sizeof(peers), "PEERS %u",
                static_cast<unsigned>(s.peers));
  c.draw_text(216, 10, peers, 2);
  draw_battery(c, kW - 46, 9, rs.battery_pct);
  c.fill_rect(12, 32, kW - 24, 2, true);

  // BPM, the reason this box exists. 6× digits leave room for the
  // menu overlay below.
  char bpm[24];
  std::snprintf(bpm, sizeof(bpm), "%u.%u",
                static_cast<unsigned>(s.milli_bpm / 1000u),
                static_cast<unsigned>((s.milli_bpm / 100u) % 10u));
  const int bpm_w = c.draw_text(12, 52, bpm, 6);
  c.draw_text(12 + bpm_w + 12, 73, "BPM", 3);

  if (s.overlay == 0) {
    // Beat dots: one box per quantum beat, the current one filled.
    // A reflective LCD repaints in milliseconds without flashing, so
    // unlike the e-paper face this glass can be a metronome.
    const uint32_t quantum =
        rs.quantum >= 1 && rs.quantum <= 8 ? rs.quantum : 4;
    const int dot = 26;
    const int gap = 10;
    for (uint32_t i = 0; i < quantum; ++i) {
      const int x = 12 + static_cast<int>(i) * (dot + gap);
      const int y = 118;
      if (s.playing && rs.beat == i + 1) {
        c.fill_rect(x, y, dot, dot, true);
      } else {
        c.draw_rect(x, y, dot, dot, 2, true);
      }
    }

    c.draw_text(12, 168, s.playing ? "PLAYING" : "STOPPED", 4);
  }

  // Network column, right side under the header.
  const bool show_ap = (s.setup_ap || !s.provisioned) && s.ap_pass[0] != '\0';
  if (s.overlay == 0) {
    if (show_ap) {
      // Physical access is the credential: print the setup AP password
      // on the glass whenever the box is offering that network.
      c.draw_text(12, 214, s.ap_ssid[0] != '\0' ? s.ap_ssid : "SETUP AP", 2);
      c.draw_text(12, 238, s.ap_pass, 3);
    } else {
      const char* net = !s.provisioned ? "UNPROVISIONED"
                        : s.wifi_up    ? s.ssid
                                       : "CONNECTING";
      c.draw_text(12, 222, net, 2);
    }
  }

  if (s.overlay == 5) {
    // Tempo screen: the big BPM above already tracks each nudge; here we
    // just name the screen and say which button goes which way, since a
    // reflective panel can afford a couple of instruction lines.
    c.draw_text(12, 118, "TEMPO", 4);
    c.draw_text(12, 168, "BOOT = UP", 3);
    c.draw_text(12, 205, "KEY  = DOWN", 3);
    c.draw_text(12, 246, "HOLD TO RAMP", 2);
  }

  if (s.overlay == 1 || s.overlay == 2) {
    const int row0 = 116;
    const int row_h = 22;
    for (int i = 0; i < s.n_items && i < 8; ++i) {
      const int y = row0 + i * row_h;
      char line[48];
      std::snprintf(line, sizeof(line), "%-9s %s", s.item_label[i],
                    s.item_value[i]);
      c.draw_text(16, y, line, 2);
      if (i == s.cursor) {
        c.invert_rect(10, y - 3, 300, row_h - 2);
      }
    }
    c.draw_text(330, 116, s.overlay == 2 ? "EDIT" : "MENU", 2);
  } else if (s.overlay == 3) {
    c.fill_rect(70, 82, 260, 136, true);
    c.fill_rect(74, 86, 252, 128, false);
    static const char* kPower[3] = {"RESTART", "POWER OFF", "CANCEL"};
    for (int i = 0; i < 3; ++i) {
      const int y = 98 + i * 38;
      c.draw_text(104, y, kPower[i], 3);
      if (i == s.power_cursor) {
        c.invert_rect(84, y - 5, 232, 32);
      }
    }
  }

  // Footer detail line.
  if (s.overlay != 3 && s.overlay != 5) {
    if (s.detail[0] != '\0') {
      c.draw_text(12, 276, s.detail, 2);
    } else {
      c.draw_text(12, 276, "MIDI CLOCK  24 PPQN  TRS-A", 2);
    }
  }

  if (s.invert) {
    c.invert();
  }
}

}  // namespace neon
