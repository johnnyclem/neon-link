#include "neon/gfx/rlcd_canvas.hpp"

#include "neon/config/model.hpp"
#include "neon/gfx/font5x7.hpp"

#include <cstdio>
#include <cstring>

namespace neon {

namespace {
// Logical (x, y) -> physical buffer (px, py). Landscape is identity;
// portrait rotates the logical 300×400 plane into the physical 400×300
// buffer so that, composed with the packer's built-in R1 rotation, the
// image lands upright with the three buttons on the LEFT — the way the
// panel sits in a portrait stand (buttons are on top in landscape, so a
// counter-clockwise turn to portrait moves them to the left edge).
inline void to_physical(RlcdCanvas::Orientation o, int x, int y, int* px,
                        int* py) {
  if (o == RlcdCanvas::Orientation::kPortrait) {
    *px = RlcdCanvas::kWidth - 1 - y;
    *py = x;
  } else {
    *px = x;
    *py = y;
  }
}
}  // namespace

void RlcdCanvas::set_pixel(int x, int y, bool ink) {
  int px, py;
  to_physical(orient_, x, y, &px, &py);
  if (px < 0 || px >= kWidth || py < 0 || py >= kHeight) {
    return;
  }
  uint8_t* p = &buf_[static_cast<size_t>(py) * kStride +
                     static_cast<size_t>(px / 8)];
  const uint8_t bit = static_cast<uint8_t>(0x80u >> (px & 7));
  if (ink) {
    *p = static_cast<uint8_t>(*p & ~bit);  // black
  } else {
    *p = static_cast<uint8_t>(*p | bit);  // white
  }
}

bool RlcdCanvas::pixel(int x, int y) const {
  int px, py;
  to_physical(orient_, x, y, &px, &py);
  if (px < 0 || px >= kWidth || py < 0 || py >= kHeight) {
    return false;
  }
  const uint8_t b = buf_[static_cast<size_t>(py) * kStride +
                         static_cast<size_t>(px / 8)];
  return (b & (0x80u >> (px & 7))) == 0;  // true = black
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

// Chunky battery outline. When charging it carries a lightning bolt (on
// external power); otherwise a proportional fill, or a "?" if unknown.
void draw_battery(RlcdCanvas& c, int x, int y, int pct, bool charging) {
  c.draw_rect(x, y, 34, 16, 2, true);
  c.fill_rect(x + 34, y + 4, 4, 8, true);  // positive nub
  if (charging) {
    // Lightning bolt: a left-stepping zig-zag inside the body.
    c.fill_rect(x + 16, y + 3, 9, 2, true);
    c.fill_rect(x + 11, y + 6, 12, 2, true);
    c.fill_rect(x + 14, y + 8, 9, 2, true);
    c.fill_rect(x + 18, y + 10, 4, 2, true);
    return;
  }
  if (pct < 0) {
    c.draw_text(x + 12, y + 4, "?", 1);
    return;
  }
  const int fill = (pct > 100 ? 100 : pct) * 28 / 100;
  if (fill > 0) {
    c.fill_rect(x + 3, y + 3, fill, 10, true);
  }
}

int text_w(const char* s, int scale) {
  return static_cast<int>(std::strlen(s)) * 6 * scale;
}

// The three physical buttons sit in a tight cluster around the middle
// of the button edge — top in landscape (KEY, PWR, BOOT left to right),
// left in portrait (BOOT, PWR, KEY top to bottom, the CCW turn). They
// are not at the corners, which is why a corner-pinned tab reads as a
// random floating box. ~10 mm spacing on the 84 mm long edge → 112 px
// of the 400 px axis.
static constexpr int kButtonClusterPx = 112;

static void draw_edge_tick(RlcdCanvas& c, bool portrait, int pos) {
  if (portrait) {
    c.fill_rect(0, pos - 2, 8, 5, true);
  } else {
    c.fill_rect(pos - 2, 0, 5, 8, true);
  }
}

// Startup / shutdown cheat-sheet. Silkscreen carries these on the live
// face, so the labels only appear here — one line per button, pinned to
// the actual cluster rather than the corners.
static void draw_splash_button_legend(RlcdCanvas& c) {
  const bool portrait = c.orientation() == RlcdCanvas::Orientation::kPortrait;
  const int w = c.width();
  const int h = c.height();
  const int mid = portrait ? h / 2 : w / 2;
  const int boot_or_key = mid - kButtonClusterPx / 2;
  const int pwr = mid;
  const int key_or_boot = mid + kButtonClusterPx / 2;

  if (portrait) {
    // Top to bottom along the left edge: BOOT, PWR, KEY.
    const int pos[3] = {boot_or_key, pwr, key_or_boot};
    const char* lab[3] = {"BPM+  HOLD TEMPO", "PWR",
                          "START/STOP  HOLD MENU"};
    for (int i = 0; i < 3; ++i) {
      draw_edge_tick(c, true, pos[i]);
      c.draw_text(12, pos[i] - 7, lab[i], 2);
    }
    return;
  }

  // Landscape: ticks on the top edge at KEY, PWR, BOOT. The cluster is
  // only ~112 px across, so the three single-line labels cannot sit
  // side-by-side; they stack just under the ticks in left-to-right order.
  draw_edge_tick(c, false, boot_or_key);  // KEY (left)
  draw_edge_tick(c, false, pwr);
  draw_edge_tick(c, false, key_or_boot);  // BOOT (right)
  const char* lab[3] = {"START/STOP  HOLD MENU", "PWR", "BPM+  HOLD TEMPO"};
  const int scale = 2;
  const int line_h = 7 * scale + 8;
  int y = 14;
  for (int i = 0; i < 3; ++i) {
    const int tw = text_w(lab[i], scale);
    c.draw_text((w - tw) / 2, y, lab[i], scale);
    y += line_h;
  }
}

// Draws `s` horizontally centered; returns the x it started at.
int draw_text_centered(RlcdCanvas& c, int y, const char* s, int scale) {
  const int x = (c.width() - text_w(s, scale)) / 2;
  c.draw_text(x, y, s, scale);
  return x;
}

// The largest glyph scale that keeps `len` characters inside `max_w`,
// clamped to [1, cap].
int fit_scale(int len, int max_w, int cap) {
  int s = max_w / (len * 6);
  if (s > cap) {
    s = cap;
  }
  return s < 1 ? 1 : s;
}

// draw_text with a gap carved between the glyph pixels, so big digits come
// out as a dot-matrix / pixel-block display rather than solid strokes.
int draw_text_dotted(RlcdCanvas& c, int x, int y, const char* s, int scale) {
  if (s == nullptr || scale < 2) {
    return c.draw_text(x, y, s, scale < 1 ? 1 : scale);
  }
  const int gap = scale >= 6 ? scale / 3 : 1;
  int cx = x;
  for (const char* p = s; *p != '\0'; ++p) {
    const uint8_t* g = gfx::glyph5x7(*p);
    for (int col = 0; col < 5; ++col) {
      const uint8_t bits = g[col];
      for (int row = 0; row < 7; ++row) {
        if ((bits >> row) & 1u) {
          c.fill_rect(cx + col * scale, y + row * scale, scale - gap,
                      scale - gap, true);
        }
      }
    }
    cx += 6 * scale;
  }
  return cx - x;
}

// Milli-BPM as "128.0".
void bpm_text(const LinkSyncPanelStatus& s, char* buf, size_t cap) {
  std::snprintf(buf, cap, "%u.%u", static_cast<unsigned>(s.milli_bpm / 1000u),
                static_cast<unsigned>((s.milli_bpm / 100u) % 10u));
}

uint32_t safe_quantum(const RlcdPanelStatus& rs) {
  return rs.quantum >= 1 && rs.quantum <= 8 ? rs.quantum : 4;
}

// One box per quantum beat, the current one filled, centered on `w`.
void beat_row_centered(RlcdCanvas& c, const RlcdPanelStatus& rs, int y,
                       int dot, int gap) {
  const LinkSyncPanelStatus& s = rs.base;
  const uint32_t quantum = safe_quantum(rs);
  const int total =
      static_cast<int>(quantum) * dot + (static_cast<int>(quantum) - 1) * gap;
  const int x0 = (c.width() - total) / 2;
  for (uint32_t i = 0; i < quantum; ++i) {
    const int x = x0 + static_cast<int>(i) * (dot + gap);
    if (s.playing && rs.beat == i + 1) {
      c.fill_rect(x, y, dot, dot, true);
    } else {
      c.draw_rect(x, y, dot, dot, 2, true);
    }
  }
}

// Setup credentials, pinned to the bottom edge. The minimal faces drop the
// classic network column, but a box that is offering its setup AP must
// still print the way in — physical access is the credential.
void setup_footer(RlcdCanvas& c, const LinkSyncPanelStatus& s) {
  const bool show_ap = (s.setup_ap || !s.provisioned) && s.ap_pass[0] != '\0';
  if (!show_ap) {
    return;
  }
  char line[sizeof(s.ap_ssid) + sizeof(s.ap_pass) + 2];
  std::snprintf(line, sizeof(line), "%s  %s",
                s.ap_ssid[0] != '\0' ? s.ap_ssid : "SETUP AP", s.ap_pass);
  const int scale = text_w(line, 2) <= c.width() - 8 ? 2 : 1;
  draw_text_centered(c, c.height() - (scale == 2 ? 22 : 14), line, scale);
}

}  // namespace

void render_rlcd_splash(RlcdCanvas& c) {
  c.clear();
  const bool portrait = c.orientation() == RlcdCanvas::Orientation::kPortrait;
  const int kW = c.width();

  // Button cheat-sheet first so the wordmark can sit around the cluster
  // (above it in portrait, below it in landscape).
  draw_splash_button_legend(c);

  const char* mark = "NEON LINK";
  const int mark_scale = portrait ? 4 : 6;  // 9 chars must fit the width
  const int mark_w = static_cast<int>(std::strlen(mark)) * 6 * mark_scale;
  const int mark_x = (kW - mark_w) / 2;
  const int mark_y = portrait ? 36 : 120;
  c.fill_rect(mark_x - 10, mark_y - 10, mark_w + 20, 4, true);
  c.draw_text(mark_x, mark_y, mark, mark_scale);
  c.fill_rect(mark_x - 10, mark_y + 7 * mark_scale + 6, mark_w + 20, 4, true);

  const char* line = "press a button to start";
  const int line_scale = 2;
  const int line_w = static_cast<int>(std::strlen(line)) * 6 * line_scale;
  c.draw_text((kW - line_w) / 2, portrait ? 348 : 230, line, line_scale);
}

// Transport word for the live face. STOPPING is the quantized-stop window
// (a stop was pressed and is playing out the current bar) so the press
// registers at once rather than after up to a whole bar.
static const char* transport_label(const RlcdPanelStatus& rs) {
  if (rs.starting) {
    return "";  // the count-in banner carries the message; metronome animates
  }
  if (rs.stopping) {
    return "STOPPING";
  }
  return rs.base.playing ? "PLAYING" : "STOPPED";
}

// The 400×300 landscape status face — the original layout, unchanged.
static void render_landscape(RlcdCanvas& c, const RlcdPanelStatus& rs) {
  const LinkSyncPanelStatus& s = rs.base;
  c.clear();
  constexpr int kW = RlcdCanvas::kWidth;

  // Header: name left, peers + battery right, rule underneath.
  c.draw_text(12, 10, s.title[0] != '\0' ? s.title : "link-rlcd", 2);
  char peers[16];
  std::snprintf(peers, sizeof(peers), "PEERS %u",
                static_cast<unsigned>(s.peers));
  c.draw_text(216, 10, peers, 2);
  draw_battery(c, kW - 46, 9, rs.battery_pct, s.usb_power);
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

    c.draw_text(12, 168, transport_label(rs), 4);
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
    // Tempo screen: the big BPM above tracks each nudge; BOOT is up and
    // KEY is down (the silkscreen and splash teach the live mapping).
    c.draw_text(12, 118, "TEMPO", 4);
    c.draw_text(12, 170, "HOLD TO RAMP", 2);
  }

  if (s.overlay == 1 || s.overlay == 2) {
    const int row0 = 112;
    const int row_h = 20;
    for (int i = 0; i < s.n_items && i < 10; ++i) {
      const int y = row0 + i * row_h;
      char line[48];
      std::snprintf(line, sizeof(line), "%-9s %s", s.item_label[i],
                    s.item_value[i]);
      c.draw_text(16, y, line, 2);
      if (i == s.cursor) {
        c.invert_rect(10, y - 3, 300, row_h - 2);
      }
    }
    c.draw_text(330, 112, s.overlay == 2 ? "EDIT" : "MENU", 2);
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

  // Footer detail line (live face only; overlays own the lower rows).
  if (s.overlay == 0) {
    if (s.detail[0] != '\0') {
      c.draw_text(12, 276, s.detail, 2);
    } else {
      c.draw_text(12, 276, "MIDI CLOCK  24 PPQN  TRS-A", 2);
    }
  }
}

// The 300×400 portrait status face. Same information, reflowed tall so it
// reads upright in a portrait stand. Coordinates are logical; the canvas
// rotates them into the physical buffer.
static void render_portrait(RlcdCanvas& c, const RlcdPanelStatus& rs) {
  const LinkSyncPanelStatus& s = rs.base;
  c.clear();
  const int kW = c.width();  // 300

  // Header: name left, battery right, rule underneath.
  c.draw_text(10, 10, s.title[0] != '\0' ? s.title : "link-rlcd", 2);
  draw_battery(c, kW - 44, 9, rs.battery_pct, s.usb_power);
  c.fill_rect(10, 34, kW - 20, 2, true);

  // BPM, the reason this box exists.
  char bpm[24];
  std::snprintf(bpm, sizeof(bpm), "%u.%u",
                static_cast<unsigned>(s.milli_bpm / 1000u),
                static_cast<unsigned>((s.milli_bpm / 100u) % 10u));
  const int bpm_w = c.draw_text(14, 60, bpm, 5);
  c.draw_text(14 + bpm_w + 10, 74, "BPM", 3);

  if (s.overlay == 0) {
    // Beat dots: one box per quantum beat, the current one filled.
    const uint32_t quantum =
        rs.quantum >= 1 && rs.quantum <= 8 ? rs.quantum : 4;
    const int dot = 24;
    const int gap = 9;
    for (uint32_t i = 0; i < quantum; ++i) {
      const int x = 14 + static_cast<int>(i) * (dot + gap);
      const int y = 120;
      if (s.playing && rs.beat == i + 1) {
        c.fill_rect(x, y, dot, dot, true);
      } else {
        c.draw_rect(x, y, dot, dot, 2, true);
      }
    }
    c.draw_text(14, 170, transport_label(rs), 4);

    char peers[16];
    std::snprintf(peers, sizeof(peers), "PEERS %u",
                  static_cast<unsigned>(s.peers));
    c.draw_text(14, 220, peers, 2);

    const bool show_ap =
        (s.setup_ap || !s.provisioned) && s.ap_pass[0] != '\0';
    if (show_ap) {
      c.draw_text(14, 250, s.ap_ssid[0] != '\0' ? s.ap_ssid : "SETUP AP", 2);
      c.draw_text(14, 276, s.ap_pass, 3);
    } else {
      const char* net = !s.provisioned ? "UNPROVISIONED"
                        : s.wifi_up    ? s.ssid
                                       : "CONNECTING";
      c.draw_text(14, 250, net, 2);
    }
  }

  if (s.overlay == 5) {
    c.draw_text(14, 120, "TEMPO", 4);
    c.draw_text(14, 172, "HOLD TO RAMP", 2);
  }

  if (s.overlay == 1 || s.overlay == 2) {
    c.draw_text(14, 98, s.overlay == 2 ? "EDIT" : "MENU", 2);
    // Rows sit between the top BOOT tab and the bottom KEY tab (all nine
    // settings must clear the KEY tab that starts at height-44).
    const int row0 = 116;
    const int row_h = 26;
    for (int i = 0; i < s.n_items && i < 10; ++i) {
      const int y = row0 + i * row_h;
      char line[48];
      std::snprintf(line, sizeof(line), "%-9s %s", s.item_label[i],
                    s.item_value[i]);
      c.draw_text(16, y, line, 2);
      if (i == s.cursor) {
        c.invert_rect(10, y - 4, kW - 20, row_h - 2);
      }
    }
  } else if (s.overlay == 3) {
    c.fill_rect(30, 120, 240, 176, true);
    c.fill_rect(34, 124, 232, 168, false);
    static const char* kPower[3] = {"RESTART", "POWER OFF", "CANCEL"};
    for (int i = 0; i < 3; ++i) {
      const int y = 140 + i * 48;
      c.draw_text(56, y, kPower[i], 3);
      if (i == s.power_cursor) {
        c.invert_rect(44, y - 6, 212, 34);
      }
    }
  }

  // Footer detail line (live face only).
  if (s.overlay == 0) {
    c.draw_text(14, 372, s.detail[0] != '\0' ? s.detail
                                             : "MIDI CLOCK  24 PPQN", 2);
  }
}

// ---------------------------------------------------------------------------
// Theme faces. Each draws the LIVE status face (overlay 0) only, ink on
// white, using logical width()/height() so one layout serves both
// orientations. The dark themes are flipped by the caller via base.invert;
// PULSE manages its own accent flash.

// INK (designer mockup 4): the chrome-free light face. Peers and battery up
// top, then nothing but the tempo, the beat, and the transport state.
static void render_theme_ink(RlcdCanvas& c, const RlcdPanelStatus& rs) {
  const LinkSyncPanelStatus& s = rs.base;
  c.clear();
  const int w = c.width();
  const int h = c.height();

  char peers[16];
  std::snprintf(peers, sizeof(peers), "PEERS %u",
                static_cast<unsigned>(s.peers));
  c.draw_text(12, 12, peers, 2);
  draw_battery(c, w - 50, 10, rs.battery_pct, s.usb_power);

  char bpm[24];
  bpm_text(s, bpm, sizeof(bpm));
  const int scale = fit_scale(static_cast<int>(std::strlen(bpm)), w - 28, 12);
  const int bpm_y = h / 2 - (7 * scale) / 2 - h / 10;
  draw_text_centered(c, bpm_y, bpm, scale);

  const int dot = 30;
  beat_row_centered(c, rs, bpm_y + 7 * scale + 26, dot, 12);

  draw_text_centered(c, h - 74, transport_label(rs), 4);
  setup_footer(c, s);
}

// DOTS (designer mockup 1): dark, dot-matrix hero digits. Peers and
// battery live in the header; button functions are silkscreened (and
// taught on the splash), so this face does not pin a three-column
// legend to the button edge.
static void render_theme_dots(RlcdCanvas& c, const RlcdPanelStatus& rs) {
  const LinkSyncPanelStatus& s = rs.base;
  c.clear();
  const int w = c.width();
  const int h = c.height();

  char peers[16];
  std::snprintf(peers, sizeof(peers), "PEERS %u",
                static_cast<unsigned>(s.peers));
  c.draw_text(12, 12, peers, 2);
  draw_battery(c, w - 50, 10, rs.battery_pct, s.usb_power);

  char bpm[24];
  bpm_text(s, bpm, sizeof(bpm));
  const int scale = fit_scale(static_cast<int>(std::strlen(bpm)), w - 24, 12);
  const int bpm_y = h / 2 - (7 * scale) / 2 - h / 12;
  draw_text_dotted(c, (w - text_w(bpm, scale)) / 2, bpm_y, bpm, scale);

  beat_row_centered(c, rs, bpm_y + 7 * scale + 30, 34, 12);

  draw_text_centered(c, h - 64, transport_label(rs), 4);
  setup_footer(c, s);
}

// HERO (designer mockup 2): one giant integer BPM and almost nothing else.
static void render_theme_hero(RlcdCanvas& c, const RlcdPanelStatus& rs) {
  const LinkSyncPanelStatus& s = rs.base;
  c.clear();
  const int w = c.width();
  const int h = c.height();

  char peers[16];
  std::snprintf(peers, sizeof(peers), "PEERS %u",
                static_cast<unsigned>(s.peers));
  c.draw_text(12, 12, peers, 2);
  draw_battery(c, w - 50, 10, rs.battery_pct, s.usb_power);

  char bpm[8];
  std::snprintf(bpm, sizeof(bpm), "%u",
                static_cast<unsigned>((s.milli_bpm + 500u) / 1000u));
  const int len = static_cast<int>(std::strlen(bpm));
  int scale = (h - 130) / 7;
  if (scale > (w - 20) / (len * 6)) {
    scale = (w - 20) / (len * 6);
  }
  const int bpm_y = 40 + (h - 130 - 7 * scale) / 2;
  draw_text_centered(c, bpm_y, bpm, scale);

  // Beat ticks: one small mark per quantum beat, the current one grown.
  const uint32_t quantum = safe_quantum(rs);
  const int gap = 22;
  const int x0 = (w - (static_cast<int>(quantum) - 1) * gap) / 2;
  for (uint32_t i = 0; i < quantum; ++i) {
    const int x = x0 + static_cast<int>(i) * gap;
    if (s.playing && rs.beat == i + 1) {
      c.fill_rect(x - 3, h - 96, 6, 20, true);
    } else {
      c.fill_rect(x - 1, h - 90, 3, 12, true);
    }
  }

  draw_text_centered(c, h - 56, transport_label(rs), 3);
  setup_footer(c, s);
}

// CONSOLE (designer mockup 3): the instrument panel. LINK with a session
// box, a data column (current beat / peers / battery), the tempo, and a
// RUN / STOP indicator with the active word underlined.
static void render_theme_console(RlcdCanvas& c, const RlcdPanelStatus& rs) {
  const LinkSyncPanelStatus& s = rs.base;
  c.clear();
  const int w = c.width();
  const int h = c.height();
  const bool portrait = h > w;

  c.draw_text(14, 14, "LINK", 4);
  c.draw_rect(w - 48, 10, 34, 34, 3, true);
  if (s.peers > 0) {
    c.fill_rect(w - 41, 17, 20, 20, true);
  }

  char tap[12];
  char np[12];
  char batt[12];
  std::snprintf(tap, sizeof(tap), "%u", static_cast<unsigned>(rs.beat));
  std::snprintf(np, sizeof(np), "%u", static_cast<unsigned>(s.peers));
  if (rs.battery_pct < 0) {
    std::snprintf(batt, sizeof(batt), "?");
  } else {
    std::snprintf(batt, sizeof(batt), "%d", rs.battery_pct);
  }
  const char* labels[3] = {"TAP", "PEERS", "BATT"};
  const char* values[3] = {tap, np, batt};
  const int col_y0 = portrait ? 90 : 76;
  const int col_dy = portrait ? 72 : 66;
  for (int i = 0; i < 3; ++i) {
    const int y = col_y0 + i * col_dy;
    c.draw_text(14, y, labels[i], 2);
    c.draw_text(14, y + 20, values[i], 3);
  }

  char bpm[24];
  bpm_text(s, bpm, sizeof(bpm));
  const int bx = portrait ? 104 : 120;
  const int bscale = fit_scale(static_cast<int>(std::strlen(bpm)), w - bx - 10,
                               portrait ? 6 : 7);
  c.draw_text(bx, portrait ? 100 : 84, bpm, bscale);

  // Beat boxes under the tempo, aligned with it.
  const uint32_t quantum = safe_quantum(rs);
  const int dot = portrait ? 22 : 26;
  const int gap = portrait ? 9 : 10;
  const int dots_y = (portrait ? 100 : 84) + 7 * bscale + 26;
  for (uint32_t i = 0; i < quantum; ++i) {
    const int x = bx + static_cast<int>(i) * (dot + gap);
    if (s.playing && rs.beat == i + 1) {
      c.fill_rect(x, dots_y, dot, dot, true);
    } else {
      c.draw_rect(x, dots_y, dot, dot, 2, true);
    }
  }

  // RUN / STOP, active word underlined.
  const char* run = "RUN";
  const char* sep = " / ";
  const char* stop = "STOP";
  const int rs_scale = 3;
  const int total =
      text_w(run, rs_scale) + text_w(sep, rs_scale) + text_w(stop, rs_scale);
  const int rx = (w - total) / 2;
  const int ry = h - 52;
  c.draw_text(rx, ry, run, rs_scale);
  c.draw_text(rx + text_w(run, rs_scale), ry, sep, rs_scale);
  const int stop_x = rx + text_w(run, rs_scale) + text_w(sep, rs_scale);
  c.draw_text(stop_x, ry, stop, rs_scale);
  if (s.playing) {
    c.fill_rect(rx, ry + 7 * rs_scale + 5, text_w(run, rs_scale) - rs_scale, 4,
                true);
  } else {
    c.fill_rect(stop_x, ry + 7 * rs_scale + 5, text_w(stop, rs_scale) - rs_scale,
                4, true);
  }
  setup_footer(c, s);
}

// GRID (designer mockup 5): everything in boxes. Title bar, tempo row, one
// tall cell per beat, an inverted state banner, and a peer tick row.
static void render_theme_grid(RlcdCanvas& c, const RlcdPanelStatus& rs) {
  const LinkSyncPanelStatus& s = rs.base;
  c.clear();
  const int w = c.width();
  const int h = c.height();
  const bool portrait = h > w;
  const int m = 10;

  // Title bar with the device name and the battery inside it.
  c.draw_rect(m, m, w - 2 * m, 36, 2, true);
  char name[24];
  std::snprintf(name, sizeof(name), "%s",
                s.title[0] != '\0' ? s.title : "NEON LINK");
  for (char* p = name; *p != '\0'; ++p) {
    if (*p >= 'a' && *p <= 'z') {
      *p = static_cast<char>(*p - 'a' + 'A');
    }
  }
  c.draw_text(m + 10, m + 11, name, 2);
  draw_battery(c, w - m - 46, m + 10, rs.battery_pct, s.usb_power);

  // Tempo row.
  const int bpm_y = m + 48;
  c.draw_text(m + 6, bpm_y + 14, "BPM", 3);
  char bpm[24];
  bpm_text(s, bpm, sizeof(bpm));
  c.draw_text(m + 6 + text_w("BPM", 3) + 10, bpm_y, bpm, 6);

  // One tall bordered cell per quantum beat, the current one filled.
  const uint32_t quantum = safe_quantum(rs);
  const int cells_y = bpm_y + 58;
  const int cell_h = portrait ? 128 : 66;
  const int cell_gap = 8;
  const int cell_w =
      (w - 2 * m - (static_cast<int>(quantum) - 1) * cell_gap) /
      static_cast<int>(quantum);
  for (uint32_t i = 0; i < quantum; ++i) {
    const int x = m + static_cast<int>(i) * (cell_w + cell_gap);
    if (s.playing && rs.beat == i + 1) {
      c.fill_rect(x, cells_y, cell_w, cell_h, true);
    } else {
      c.draw_rect(x, cells_y, cell_w, cell_h, 2, true);
    }
  }

  // State banner: black bar, knocked-out text.
  const int banner_y = cells_y + cell_h + 12;
  draw_text_centered(c, banner_y + 7, transport_label(rs), 3);
  c.invert_rect(m, banner_y, w - 2 * m, 34);

  // Peer ticks: six boxes, one slash per connected peer.
  const int peers_y = banner_y + 44;
  c.draw_text(m + 2, peers_y + 3, "PEERS", 2);
  const int tick = 20;
  int tx = m + 2 + text_w("PEERS", 2) + 10;
  for (int i = 0; i < 6; ++i) {
    c.draw_rect(tx, peers_y, tick, tick, 2, true);
    if (static_cast<uint32_t>(i) < s.peers) {
      for (int d = 0; d < tick - 8; ++d) {
        c.fill_rect(tx + 4 + d, peers_y + tick - 6 - d, 2, 2, true);
      }
    }
    tx += tick + 6;
  }
  setup_footer(c, s);
}

// PULSE (ours): the metronome face. While playing, the whole screen is the
// beat — a giant count that flashes inverted on the one, readable from the
// back of the stage. Stopped, it settles into a big-BPM standby.
static void render_theme_pulse(RlcdCanvas& c, const RlcdPanelStatus& rs) {
  const LinkSyncPanelStatus& s = rs.base;
  c.clear();
  const int w = c.width();
  const int h = c.height();

  char peers[16];
  std::snprintf(peers, sizeof(peers), "PEERS %u",
                static_cast<unsigned>(s.peers));
  char bpm[24];
  bpm_text(s, bpm, sizeof(bpm));

  if (!s.playing) {
    c.draw_text(12, 12, peers, 2);
    draw_battery(c, w - 50, 10, rs.battery_pct, s.usb_power);
    const int scale = fit_scale(static_cast<int>(std::strlen(bpm)), w - 28, 10);
    draw_text_centered(c, h / 2 - (7 * scale) / 2 - 20, bpm, scale);
    draw_text_centered(c, h - 74, transport_label(rs), 4);
    setup_footer(c, s);
    return;
  }

  c.draw_text(12, 12, bpm, 2);
  c.draw_text(w - 12 - text_w(peers, 2), 12, peers, 2);

  char digit[8];
  std::snprintf(digit, sizeof(digit), "%u", static_cast<unsigned>(rs.beat));
  int scale = (h - 90) / 7;
  if (scale > (w - 20) / 6) {
    scale = (w - 20) / 6;
  }
  draw_text_centered(c, 44 + (h - 100 - 7 * scale) / 2, digit, scale);

  const uint32_t quantum = safe_quantum(rs);
  const int gap = 22;
  const int x0 = (w - (static_cast<int>(quantum) - 1) * gap) / 2;
  for (uint32_t i = 0; i < quantum; ++i) {
    const int x = x0 + static_cast<int>(i) * gap;
    if (rs.beat == i + 1) {
      c.fill_rect(x - 3, h - 34, 6, 18, true);
    } else {
      c.fill_rect(x - 1, h - 29, 3, 10, true);
    }
  }

  if (rs.beat == 1) {
    c.invert();  // accent flash on the one
  }
}

// Count-in banner over the running metronome: "STARTING IN N", sized to
// fit and boxed so it reads on any theme in either orientation.
static void draw_countin_banner(RlcdCanvas& c, unsigned n) {
  char t[16];
  std::snprintf(t, sizeof(t), "STARTING IN %u", n);
  const int w = c.width();
  const int scale = fit_scale(static_cast<int>(std::strlen(t)), w - 24, 3);
  const int tw = text_w(t, scale);
  const int th = 7 * scale;
  const int y = (c.height() * 3) / 5;
  c.fill_rect((w - tw) / 2 - 8, y - 6, tw + 16, th + 12, false);
  c.draw_rect((w - tw) / 2 - 8, y - 6, tw + 16, th + 12, 2, true);
  draw_text_centered(c, y, t, scale);
}

void render_rlcd_panel(RlcdCanvas& c, const RlcdPanelStatus& rs) {
  if (rs.base.overlay == 4) {
    render_rlcd_splash(c);
    if (rs.base.invert) {
      c.invert();
    }
    return;
  }
  if (rs.base.overlay == 0) {
    // The live face is the theme's to draw; CLASSIC and NIGHT share the
    // original layout (NIGHT is the invert flag doing the work).
    switch (static_cast<MonoTheme>(rs.theme)) {
      case MonoTheme::kInk:
        render_theme_ink(c, rs);
        break;
      case MonoTheme::kDots:
        render_theme_dots(c, rs);
        break;
      case MonoTheme::kHero:
        render_theme_hero(c, rs);
        break;
      case MonoTheme::kConsole:
        render_theme_console(c, rs);
        break;
      case MonoTheme::kGrid:
        render_theme_grid(c, rs);
        break;
      case MonoTheme::kPulse:
        render_theme_pulse(c, rs);
        break;
      default:
        if (c.orientation() == RlcdCanvas::Orientation::kPortrait) {
          render_portrait(c, rs);
        } else {
          render_landscape(c, rs);
        }
        break;
    }
  } else if (c.orientation() == RlcdCanvas::Orientation::kPortrait) {
    render_portrait(c, rs);
  } else {
    render_landscape(c, rs);
  }
  // Count-in banner over the animating metronome, before the theme's
  // dark-flip so it inverts with the face. Button labels live on the
  // splash (and the silkscreen), not the running UI.
  if (rs.starting) {
    draw_countin_banner(c, rs.countin);
  }
  if (rs.base.invert) {
    c.invert();
  }
}

}  // namespace neon
