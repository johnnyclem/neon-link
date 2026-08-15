#include "neon/ui/widgets.hpp"

#include <cstdio>
#include <cstring>

namespace neon::ui {

namespace {

int aligned_x(int x, int width, Align align) {
  switch (align) {
    case Align::kCenter:
      return x == kAlignPanel ? (kWidth - width) / 2 : x - width / 2;
    case Align::kRight:
      return x == kAlignPanel ? kWidth - kMargin - width : x - width;
    default:
      return x == kAlignPanel ? kMargin : x;
  }
}

}  // namespace

// ---- text ---------------------------------------------------------------

void draw_label(Framebuffer& fb, int x, int y, const char* text, Align align,
                Font font) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  const int w = Framebuffer::text_width(text, font);
  fb.draw_text(aligned_x(x, w, align), y, text, font);
}

// ---- hero numerals ------------------------------------------------------

int hero_text_width(const char* text) {
  int w = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    const HeroGlyph* g = hero_glyph(*p);
    if (g == nullptr) {
      continue;
    }
    if (w != 0) {
      w += kHeroTracking;
    }
    w += g->width;
  }
  return w;
}

int draw_hero_text(Framebuffer& fb, int x, int y, const char* text) {
  int cx = x;
  bool first = true;
  for (const char* p = text; *p != '\0'; ++p) {
    const HeroGlyph* g = hero_glyph(*p);
    if (g == nullptr) {
      continue;
    }
    if (!first) {
      cx += kHeroTracking;
    }
    first = false;
    for (int col = 0; col < g->width; ++col) {
      const uint32_t bits = g->cols[col];
      for (int row = 0; row < kHeroHeight; ++row) {
        if ((bits >> row) & 1u) {
          fb.set_pixel(cx + col, y + row, true);
        }
      }
    }
    cx += g->width;
  }
  return cx - x;
}

void draw_hero_bpm(Framebuffer& fb, uint32_t milli_bpm, bool valid, int y) {
  char text[12];
  if (valid) {
    // One decimal, rounded (.5 and up goes up). 32850 → "32.9",
    // 33000 → "33.0", 119949 → "119.9", 119950 → "120.0".
    const unsigned tenths =
        static_cast<unsigned>((milli_bpm + 50u) / 100u);
    std::snprintf(text, sizeof(text), "%u.%u", tenths / 10u, tenths % 10u);
  } else {
    // Same glyph count as a three-digit tempo, so the readout does not
    // jump when the first sync lands.
    std::snprintf(text, sizeof(text), "--.-");
  }
  const int w = hero_text_width(text);
  draw_hero_text(fb, (kWidth - w) / 2, y, text);
}

// ---- chrome -------------------------------------------------------------

void draw_icon(Framebuffer& fb, int x, int y, const Icon& icon,
               const IconClocks& clocks) {
  fb.blit(x, y, icon_frame(icon, icon_frame_index(icon, clocks)), kIconSize,
          kIconSize);
}

void draw_header(Framebuffer& fb, const char* title, const Icon* const* icons,
                 int icon_count, const IconClocks& clocks) {
  draw_label(fb, kMargin, kHeaderTextY, title);

  // Icons pack right-to-left against the margin so adding one never shifts
  // the others.
  int x = kWidth - kMargin - kIconSize;
  for (int i = icon_count - 1; i >= 0; --i) {
    if (icons[i] == nullptr) {
      continue;
    }
    draw_icon(fb, x, kHeaderTextY, *icons[i], clocks);
    x -= kIconSize + 2;
  }

  fb.fill_rect(0, kHeaderRuleY, kWidth, 1, true);
}

void draw_status_row(Framebuffer& fb, int y, const char* const* words,
                     int count) {
  char line[40] = {};
  size_t len = 0;
  for (int i = 0; i < count; ++i) {
    if (words[i] == nullptr || words[i][0] == '\0') {
      continue;
    }
    const size_t need = std::strlen(words[i]) + (len != 0 ? 2 : 0);
    if (len + need >= sizeof(line)) {
      break;
    }
    if (len != 0) {
      line[len++] = ' ';
      line[len++] = ' ';
    }
    std::memcpy(line + len, words[i], std::strlen(words[i]));
    len += std::strlen(words[i]);
  }
  line[len] = '\0';
  draw_label(fb, kAlignPanel, y, line, Align::kCenter);
}

void draw_bar(Framebuffer& fb, int y, uint32_t phase_milli_beats,
              uint32_t quantum_beats, bool running, int h, int tick_h) {
  fb.rect(0, y, kWidth, h, true);

  const uint32_t quantum = quantum_beats != 0 ? quantum_beats : 4;
  const uint32_t span = quantum * 1000;
  uint32_t phase = phase_milli_beats;
  if (phase > span) {
    phase = span;
  }

  const int track = kWidth - 2 * kBarInset;
  const int fill =
      static_cast<int>(static_cast<uint64_t>(track) * phase / span);
  if (fill > 0) {
    fb.fill_rect_dither(kBarInset, y + kBarInset, fill, h - 2 * kBarInset,
                        running ? Dither::kSolid : Dither::kHalf);
  }

  for (uint32_t b = 1; b < quantum; ++b) {
    const int x =
        static_cast<int>(static_cast<uint64_t>(kWidth) * b / quantum);
    fb.fill_rect(x, y - tick_h, 1, tick_h, true);
    fb.fill_rect(x, y + h, 1, tick_h, true);
  }
}

// ---- lists --------------------------------------------------------------

void draw_focus(Framebuffer& fb, int y, int h) {
  fb.invert_rect(0, y - kFocusInset, kWidth, h + 2 * kFocusInset);
}

void draw_list_row(Framebuffer& fb, int row, const char* label,
                   const char* value, bool focused, bool editing, int top,
                   int row_h) {
  const int y = top + row * row_h;

  if (focused && !editing) {
    draw_label(fb, kMargin, y, ">");
  }
  draw_label(fb, kListGutter, y, label);
  if (value != nullptr && value[0] != '\0') {
    draw_label(fb, kAlignPanel, y, value, Align::kRight);
  }

  // Editing inverts the whole row: the encoder is changing a value, not
  // moving a cursor, and that has to be obvious without looking twice.
  if (editing) {
    draw_focus(fb, y, Framebuffer::glyph_height(kFontBody));
  }
}

// ---- confirmation -------------------------------------------------------

void draw_confirm(Framebuffer& fb, const char* title, const char* line1,
                  const char* line2, bool yes_selected,
                  const Layout& layout) {
  draw_header(fb, title);
  draw_label(fb, kAlignPanel, layout.confirm_line1_y, line1, Align::kCenter);
  draw_label(fb, kAlignPanel, layout.confirm_line2_y, line2, Align::kCenter);

  constexpr int kBoxW = 44;
  const int box_h = layout.confirm_box_h;
  const int box_y = layout.confirm_box_y;
  const int gap = (kWidth - 2 * kBoxW) / 3;
  const int no_x = gap;
  const int yes_x = 2 * gap + kBoxW;
  const int text_y = box_y + (box_h - Framebuffer::glyph_height(kFontBody)) / 2;

  fb.rect(no_x, box_y, kBoxW, box_h, true);
  draw_label(fb, no_x + kBoxW / 2, text_y, "NO", Align::kCenter);
  fb.rect(yes_x, box_y, kBoxW, box_h, true);
  draw_label(fb, yes_x + kBoxW / 2, text_y, "YES", Align::kCenter);

  const int sel_x = yes_selected ? yes_x : no_x;
  fb.invert_rect(sel_x, box_y, kBoxW, box_h);
}

// ---- giant beat ---------------------------------------------------------

namespace {

// 7×11 block digits. Bit 6 is the leftmost column. Fat enough to read
// from the other side of a rack at 11 px per cell (77×121 inside the
// 2 px border).
constexpr int kDigitW = 7;
constexpr int kDigitH = 11;
constexpr int kBeatInset = 2;

constexpr uint8_t kDigits[10][kDigitH] = {
    {0x3e, 0x7f, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x7f, 0x3e},  // 0
    {0x1e, 0x3e, 0x3c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x7f, 0x7f},  // 1
    {0x3e, 0x7f, 0x63, 0x03, 0x06, 0x1c, 0x30, 0x60, 0x60, 0x7f, 0x7f},  // 2
    {0x3e, 0x7f, 0x63, 0x03, 0x1e, 0x1e, 0x03, 0x63, 0x63, 0x7f, 0x3e},  // 3
    {0x0c, 0x1c, 0x3c, 0x6c, 0x6c, 0x7f, 0x7f, 0x0c, 0x0c, 0x0c, 0x1e},  // 4
    {0x7f, 0x7f, 0x60, 0x60, 0x7e, 0x3f, 0x03, 0x03, 0x63, 0x7f, 0x3e},  // 5
    {0x3e, 0x7f, 0x63, 0x60, 0x7e, 0x7f, 0x63, 0x63, 0x63, 0x7f, 0x3e},  // 6
    {0x7f, 0x7f, 0x03, 0x06, 0x06, 0x0c, 0x0c, 0x18, 0x18, 0x18, 0x18},  // 7
    {0x3e, 0x7f, 0x63, 0x63, 0x3e, 0x3e, 0x63, 0x63, 0x63, 0x7f, 0x3e},  // 8
    {0x3e, 0x7f, 0x63, 0x63, 0x63, 0x3f, 0x1f, 0x03, 0x63, 0x7f, 0x3e},  // 9
};

void paint_digit(Framebuffer& fb, int origin_x, int origin_y, int cell,
                 int digit, bool on) {
  if (digit < 0 || digit > 9 || cell <= 0) {
    return;
  }
  for (int row = 0; row < kDigitH; ++row) {
    const uint8_t bits = kDigits[digit][row];
    for (int col = 0; col < kDigitW; ++col) {
      if ((bits & (1u << (kDigitW - 1 - col))) == 0) {
        continue;
      }
      fb.fill_rect(origin_x + col * cell, origin_y + row * cell, cell, cell,
                   on);
    }
  }
}

void paint_border(Framebuffer& fb, int height) {
  fb.fill_rect(0, 0, kWidth, kBeatInset, false);
  fb.fill_rect(0, height - kBeatInset, kWidth, kBeatInset, false);
  fb.fill_rect(0, 0, kBeatInset, height, false);
  fb.fill_rect(kWidth - kBeatInset, 0, kBeatInset, height, false);
}

}  // namespace

void draw_giant_beat(Framebuffer& fb, uint32_t beat, int height) {
  if (beat == 0) {
    beat = 1;
  }
  const bool invert = (beat % 2u) == 0u;
  if (invert) {
    fb.fill_rect(0, 0, kWidth, height, true);
  } else {
    fb.clear();
  }
  const bool ink = !invert;

  char digits[8];
  const int n = std::snprintf(digits, sizeof(digits), "%u",
                              static_cast<unsigned>(beat));
  const int gap = 1;
  const int cols = n * kDigitW + (n - 1) * gap;
  const int inner_w = kWidth - 2 * kBeatInset;
  const int inner_h = height - 2 * kBeatInset;
  int cell = inner_h / kDigitH;
  if (cell * cols > inner_w) {
    cell = inner_w / cols;
  }
  if (cell < 1) {
    cell = 1;
  }
  const int box_w = cols * cell;
  const int box_h = kDigitH * cell;
  const int x0 = (kWidth - box_w) / 2;
  const int y0 = (height - box_h) / 2;
  int x = x0;
  for (int i = 0; i < n; ++i) {
    paint_digit(fb, x, y0, cell, digits[i] - '0', ink);
    x += (kDigitW + gap) * cell;
  }
  paint_border(fb, height);
}

}  // namespace neon::ui
