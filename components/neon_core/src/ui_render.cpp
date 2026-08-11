#include "neon/ui/render.hpp"

#include <cstdio>

namespace neon {

namespace {

using Font = Framebuffer::Font;

void render_home(const UiStatus& s, Framebuffer& fb) {
  // Compact header: brand left, peer count right.
  fb.draw_text(0, 2, "NEON", Font::kSmall);
  char peers[12];
  std::snprintf(peers, sizeof(peers), "%up",
                static_cast<unsigned>(s.peers));
  const int pw = Framebuffer::text_width(peers, Font::kSmall);
  fb.draw_text(Framebuffer::kWidth - pw, 2, peers, Font::kSmall);
  fb.fill_rect(0, 12, Framebuffer::kWidth, 1, true);

  // Large BPM, optically centered.
  char bpm[16];
  std::snprintf(bpm, sizeof(bpm), "%u.%u",
                static_cast<unsigned>(s.milli_bpm / 1000),
                static_cast<unsigned>((s.milli_bpm % 1000) / 100));
  const int bw = Framebuffer::text_width(bpm, Font::kLarge);
  fb.draw_text((Framebuffer::kWidth - bw) / 2, 30, bpm, Font::kLarge);
  const int bpm_w = Framebuffer::text_width("BPM", Font::kSmall);
  fb.draw_text((Framebuffer::kWidth - bpm_w) / 2, 56, "BPM", Font::kSmall);

  // Source · transport · net.
  const char* src = s.ext_clock ? "EXT" : "LINK";
  const char* tr = s.playing ? "PLAY" : "STOP";
  const char* net = s.setup_ap                  ? "AP"
                   : s.active_net == 1          ? "ETH"
                   : s.active_net == 2          ? "WIFI"
                                                : "OFF";
  char line[24];
  std::snprintf(line, sizeof(line), "%s  %s  %s", src, tr, net);
  const int lw = Framebuffer::text_width(line, Font::kSmall);
  fb.draw_text((Framebuffer::kWidth - lw) / 2, 78, line, Font::kSmall);

  // Editor address so you can open the page without mDNS if needed.
  if (s.ip[0] != '\0') {
    const int iw = Framebuffer::text_width(s.ip, Font::kSmall);
    fb.draw_text((Framebuffer::kWidth - iw) / 2, 90, s.ip, Font::kSmall);
  } else if (s.setup_ap) {
    const char* ap = "192.168.4.1";
    const int iw = Framebuffer::text_width(ap, Font::kSmall);
    fb.draw_text((Framebuffer::kWidth - iw) / 2, 90, ap, Font::kSmall);
  }

  // Phase bar with beat ticks.
  const int bar_y = 104;
  const int bar_h = 16;
  fb.rect(0, bar_y, Framebuffer::kWidth, bar_h, true);
  const uint32_t quantum = s.quantum_beats != 0 ? s.quantum_beats : 4;
  const uint32_t span = quantum * 1000;
  uint32_t phase = s.phase_milli_beats;
  if (phase > span) {
    phase = span;
  }
  const int fill =
      static_cast<int>(static_cast<uint64_t>(Framebuffer::kWidth - 6) *
                       phase / span);
  if (fill > 0) {
    fb.fill_rect(3, bar_y + 3, fill, bar_h - 6, true);
  }
  for (uint32_t b = 1; b < quantum; ++b) {
    const int x = static_cast<int>(static_cast<uint64_t>(Framebuffer::kWidth) *
                                   b / quantum);
    fb.fill_rect(x, bar_y - 3, 1, 3, true);
    fb.fill_rect(x, bar_y + bar_h, 1, 3, true);
  }
}

void render_list(const MenuModel& menu, const char* title, Framebuffer& fb) {
  fb.draw_text(0, 2, title, Font::kSmall);
  fb.fill_rect(0, 12, Framebuffer::kWidth, 1, true);

  const int n = menu.item_count();
  const int visible = 11;
  int first = menu.cursor() - (visible - 1);
  if (first < 0) {
    first = 0;
  }
  for (int row = 0; row < visible; ++row) {
    const int idx = first + row;
    if (idx >= n) {
      break;
    }
    const int y = 16 + row * 10;
    if (idx == menu.cursor()) {
      fb.draw_text(0, y, menu.editing() ? "*" : ">", Font::kSmall);
    }
    fb.draw_text(10, y, menu.item_label(idx), Font::kSmall);
    char val[16];
    menu.item_value(idx, val, sizeof(val));
    if (val[0] != '\0') {
      const int w = Framebuffer::text_width(val, Font::kSmall);
      fb.draw_text(Framebuffer::kWidth - w - 1, y, val, Font::kSmall);
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
      render_list(menu, "MENU", fb);
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
