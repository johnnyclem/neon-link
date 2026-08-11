#include "neon/ui/render.hpp"

#include <cstdio>

namespace neon {

namespace {

using Font = Framebuffer::Font;

void render_home(const UiStatus& s, Framebuffer& fb) {
  // Title strip.
  fb.draw_text(0, 2, "NEON LINK", Font::kSmall);
  fb.fill_rect(0, 12, Framebuffer::kWidth, 1, true);

  // Large BPM, centered on the 128×128 canvas.
  char bpm[16];
  std::snprintf(bpm, sizeof(bpm), "%u.%u",
                static_cast<unsigned>(s.milli_bpm / 1000),
                static_cast<unsigned>((s.milli_bpm % 1000) / 100));
  const int w = Framebuffer::text_width(bpm, Font::kLarge);
  fb.draw_text((Framebuffer::kWidth - w) / 2, 28, bpm, Font::kLarge);
  fb.draw_text((Framebuffer::kWidth - Framebuffer::text_width("BPM", Font::kSmall)) /
                   2,
               54, "BPM", Font::kSmall);

  // Status line: source, transport, network, peers.
  char line[32];
  const char* net = s.active_net == 1 ? "ETH" : (s.active_net == 2 ? "WIFI"
                                                                   : "----");
  std::snprintf(line, sizeof(line), "%s %s %s %up",
                s.ext_clock ? "EXT" : "LINK", s.playing ? "PLAY" : "STOP",
                net, static_cast<unsigned>(s.peers));
  fb.draw_text(0, 80, line, Font::kSmall);

  // Phase bar: quantum segments near the bottom.
  const int bar_y = 108;
  const int bar_h = 12;
  fb.rect(0, bar_y, Framebuffer::kWidth, bar_h, true);
  const uint32_t quantum = s.quantum_beats != 0 ? s.quantum_beats : 4;
  const uint32_t span = quantum * 1000;
  uint32_t phase = s.phase_milli_beats;
  if (phase > span) {
    phase = span;
  }
  const int fill =
      static_cast<int>(static_cast<uint64_t>(Framebuffer::kWidth - 4) *
                       phase / span);
  fb.fill_rect(2, bar_y + 2, fill, bar_h - 4, true);
  // Beat ticks.
  for (uint32_t b = 1; b < quantum; ++b) {
    const int x = static_cast<int>(static_cast<uint64_t>(Framebuffer::kWidth) *
                                   b / quantum);
    fb.fill_rect(x, bar_y - 4, 1, 3, true);
  }
}

void render_list(const MenuModel& menu, const char* title, Framebuffer& fb) {
  fb.draw_text(0, 0, title, Font::kSmall);
  fb.fill_rect(0, 9, Framebuffer::kWidth, 1, true);

  const int n = menu.item_count();
  // 128-tall panel: ~12 rows of 9 px after the title.
  const int visible = 12;
  int first = menu.cursor() - (visible - 1);
  if (first < 0) {
    first = 0;
  }
  for (int row = 0; row < visible; ++row) {
    const int idx = first + row;
    if (idx >= n) {
      break;
    }
    const int y = 14 + row * 9;
    if (idx == menu.cursor()) {
      fb.draw_text(0, y, menu.editing() ? "*" : ">", Font::kSmall);
    }
    fb.draw_text(8, y, menu.item_label(idx), Font::kSmall);
    char val[16];
    menu.item_value(idx, val, sizeof(val));
    if (val[0] != '\0') {
      const int w = Framebuffer::text_width(val, Font::kSmall);
      fb.draw_text(Framebuffer::kWidth - w, y, val, Font::kSmall);
    }
  }
}

}  // namespace

void render_ui(const MenuModel& menu, const UiStatus& status,
               Framebuffer& fb) {
  fb.clear();
  switch (menu.screen()) {
    case MenuModel::Screen::kHome:
      render_home(status, fb);
      break;
    case MenuModel::Screen::kMenu:
      render_list(menu, "NEON LINK", fb);
      break;
    case MenuModel::Screen::kOutputs:
      render_list(menu, "OUTPUTS", fb);
      break;
    case MenuModel::Screen::kOutputEdit: {
      char title[16];
      std::snprintf(title, sizeof(title), "CLK %d",
                    (menu.output_index() & 3) + 1);
      render_list(menu, title, fb);
      break;
    }
    case MenuModel::Screen::kSettings:
      render_list(menu, "SETTINGS", fb);
      break;
  }
}

}  // namespace neon
