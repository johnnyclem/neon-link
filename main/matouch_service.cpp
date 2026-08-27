#include "sdkconfig.h"
#include "tasks.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "board_pins.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/cst816.hpp"
#include "halesp/encoder_pcnt.hpp"
#include "halesp/lcd_gc9a01.hpp"
#include "neon/fixed_math.hpp"
#include "neon/gfx/font5x7.hpp"
#include "neon/timeline.hpp"
#include "neon/transport.hpp"
#include "neon/config/model.hpp"
#include "neon/ui/menu_model.hpp"
#include "wifi.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#if CONFIG_NEON_BOARD_LINKSYNC_MATOUCH

// Proof-of-concept face for the MaTouch ESP32-S3 1.28" Rotary. A round
// 240×240 GC9A01 shows the live Link tempo, a phase ring that sweeps once
// per bar, peer count and transport state. The bezel encoder drives it:
//   twist        ±1 BPM
//   short press  play / stop
//   long press   tap tempo
// Tapping the gear at the top opens the shared settings menu (the same
// neon::MenuModel the AMYboard / CrowPanel faces drive), navigated by
// the CST816 touch panel with the encoder as a fallback.
// See docs/LINKSYNC_MATOUCH.md.

namespace {

const char* kTag = "matouch";

constexpr int kW = halesp::kGc9a01W;   // 240
constexpr int kH = halesp::kGc9a01H;   // 240
constexpr int kCx = kW / 2;
constexpr int kCy = kH / 2;

// RGB565, byte-swapped to GC9A01 wire order (see kGc9a01SwapBytes).
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t c = static_cast<uint16_t>(((r & 0xf8) << 8) |
                                           ((g & 0xfc) << 3) | (b >> 3));
  if (halesp::kGc9a01SwapBytes) {
    return static_cast<uint16_t>((c << 8) | (c >> 8));
  }
  return c;
}

// Neon-tube palette, same family as the CrowPanel/Tab5 faces.
constexpr uint16_t kBg = rgb(0, 2, 8);
constexpr uint16_t kRingDim = rgb(0, 40, 52);
constexpr uint16_t kSurface = rgb(0, 28, 40);
constexpr uint16_t kSurface2 = rgb(0, 52, 68);
constexpr uint16_t kNeon = rgb(0, 255, 255);
constexpr uint16_t kNeonDim = rgb(0, 150, 170);
constexpr uint16_t kInk = rgb(255, 255, 255);
constexpr uint16_t kMuted = rgb(0, 190, 205);
constexpr uint16_t kHot = rgb(255, 45, 149);

uint16_t* g_fb = nullptr;

// ---- Settings state (touch + encoder over the shared MenuModel) --------

neon::Config g_cfg;
neon::MenuModel g_menu(&g_cfg);
bool g_settings = false;
int g_scroll = 0;  // first visible row index

// ---- On-device WiFi join (encoder character picker) --------------------

enum class Wifi : uint8_t { kOff, kList, kPass, kConnecting };
Wifi g_wifi = Wifi::kOff;
NeonWifiScanEntry g_aps[16];
int g_ap_count = 0;
int g_ap_sel = 0;
int g_ap_scroll = 0;
char g_join_ssid[33] = {};
bool g_join_open = false;
char g_pass[65] = {};
int g_pass_len = 0;
int g_char_idx = 0;  // index into the picker (charset + DEL + OK)
int64_t g_connect_us = 0;
int64_t g_connected_us = 0;  // first moment STA reported an IP (0 = not yet)

// Rotary character set. The two virtual slots after the printable
// characters are DEL (backspace) and OK (connect), so the whole flow is
// rotate-to-pick, click-to-commit.
const char kCharset[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    " .-_!@#$%&*+=?";
constexpr int kCharsetLen = static_cast<int>(sizeof(kCharset)) - 1;
constexpr int kSlotDel = kCharsetLen;
constexpr int kSlotOk = kCharsetLen + 1;
constexpr int kSlotCount = kCharsetLen + 2;

// Gear tap zone on the live face (top-centre).
constexpr int kGearZoneY0 = 2;
constexpr int kGearZoneY1 = 42;
constexpr int kGearZoneX0 = kCx - 52;
constexpr int kGearZoneX1 = kCx + 52;

// Settings list geometry (round-safe: rows sit in the fat middle band).
constexpr int kListY0 = 44;
constexpr int kRowH = 34;
constexpr int kRows = 4;
constexpr int kBackY0 = kListY0 + kRows * kRowH + 2;  // 182
constexpr int kBackH = 30;

void fill(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > kW) {
    w = kW - x;
  }
  if (y + h > kH) {
    h = kH - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }
  for (int yy = y; yy < y + h; ++yy) {
    uint16_t* row = g_fb + yy * kW + x;
    for (int xx = 0; xx < w; ++xx) {
      row[xx] = c;
    }
  }
}

void text(int x, int y, const char* s, int scale, uint16_t c) {
  if (s == nullptr || scale < 1) {
    return;
  }
  int cx = x;
  for (const char* p = s; *p != '\0'; ++p) {
    const uint8_t* g = neon::gfx::glyph5x7(*p);
    for (int col = 0; col < 5; ++col) {
      const uint8_t bits = g[col];
      for (int row = 0; row < 7; ++row) {
        if ((bits >> row) & 1u) {
          fill(cx + col * scale, y + row * scale, scale, scale, c);
        }
      }
    }
    cx += 6 * scale;
  }
}

int text_w(const char* s, int scale) {
  if (s == nullptr || scale < 1) {
    return 0;
  }
  return static_cast<int>(std::strlen(s)) * 6 * scale;
}

void text_cx(int cx, int y, const char* s, int scale, uint16_t c) {
  text(cx - text_w(s, scale) / 2, y, s, scale, c);
}

// Bounded copy for display strings, avoiding snprintf's format-truncation
// warning when the source buffer is larger than the on-screen field.
void clip_str(char* dst, int cap, const char* src, int maxchars) {
  int n = 0;
  while (src[n] != '\0' && n < maxchars && n < cap - 1) {
    dst[n] = src[n];
    ++n;
  }
  dst[n] = '\0';
}

void disc(int cx, int cy, int r, uint16_t c) {
  const int r2 = r * r;
  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      if (dx * dx + dy * dy <= r2) {
        fill(cx + dx, cy + dy, 1, 1, c);
      }
    }
  }
}

// 7×7 cog, LSB = left (shared with the CrowPanel face).
void draw_gear(int x, int y, int scale, uint16_t c) {
  static const uint8_t kBits[7] = {0x14, 0x3e, 0x63, 0x55, 0x63, 0x3e, 0x14};
  for (int row = 0; row < 7; ++row) {
    const uint8_t bits = kBits[row];
    for (int col = 0; col < 7; ++col) {
      if ((bits >> col) & 1u) {
        fill(x + col * scale, y + row * scale, scale, scale, c);
      }
    }
  }
}

bool in_rect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

// Phase ring: 120 dots around a circle. Dots up to `progress` (0..1) light
// bright, the rest stay dim, and a fat head marks the current position.
void phase_ring(float progress, bool playing) {
  constexpr int kDots = 120;
  constexpr float kR = 108.0f;
  const int lit = static_cast<int>(progress * kDots + 0.5f);
  for (int i = 0; i < kDots; ++i) {
    // Start at 12 o'clock, sweep clockwise.
    const float a = -static_cast<float>(M_PI) / 2.0f +
                    (2.0f * static_cast<float>(M_PI) * i) / kDots;
    const int x = kCx + static_cast<int>(kR * std::cos(a));
    const int y = kCy + static_cast<int>(kR * std::sin(a));
    const bool on = playing && i < lit;
    disc(x, y, 2, on ? kNeon : kRingDim);
  }
  if (playing) {
    const float a = -static_cast<float>(M_PI) / 2.0f +
                    (2.0f * static_cast<float>(M_PI) * lit) / kDots;
    const int x = kCx + static_cast<int>(kR * std::cos(a));
    const int y = kCy + static_cast<int>(kR * std::sin(a));
    disc(x, y, 5, kInk);
  }
}

struct Snap {
  uint32_t milli_bpm = 120000;
  uint32_t phase = 0;
  uint32_t quantum = 4;
  uint32_t beat = 1;
  uint32_t peers = 0;
  bool playing = false;
  bool wifi_up = false;
  bool provisioned = false;
  char ip[16] = {};
  char firmware[32] = {};
  char bpm[16] = {};
};

Snap snapshot() {
  Snap s{};
  const neon::Config& cfg = neon_config();
  neon::TimelineSnapshot tl{};
  timeline_bus().read(tl);
  const int64_t now = esp_timer_get_time();
  const uint64_t mpb_us = (tl.tempo_mpb_q32 + (1ull << 31)) >> 32;
  if (mpb_us != 0) {
    s.milli_bpm = neon::milli_bpm_from_mpb_us(mpb_us);
  } else if (cfg.tempo_milli_bpm != 0) {
    s.milli_bpm = cfg.tempo_milli_bpm;
  }
  s.phase = neon::phase_milli_beats(tl, now);
  s.quantum = tl.quantum_beats != 0 ? tl.quantum_beats : 4;
  s.beat = neon::beat_number(s.phase, s.quantum);
  s.playing = tl.playing != 0;
  s.peers = tl.num_peers;
  s.wifi_up = neon_wifi_sta_got_ip();
  s.provisioned = neon_wifi_has_credentials();
  const char* ssid = neon_wifi_current_ssid();
  (void)ssid;
  std::snprintf(s.bpm, sizeof(s.bpm), "%u.%u",
                static_cast<unsigned>(s.milli_bpm / 1000u),
                static_cast<unsigned>((s.milli_bpm / 100u) % 10u));
  const esp_app_desc_t* desc = esp_app_get_description();
  std::snprintf(s.firmware, sizeof(s.firmware), "%s",
                desc != nullptr ? desc->version : "?");
  return s;
}

// ---- Live face ---------------------------------------------------------

void paint_live(const Snap& s) {
  fill(0, 0, kW, kH, kBg);

  const uint32_t span = s.quantum * 1000u;
  const float progress =
      span != 0 ? static_cast<float>(s.phase % span) / static_cast<float>(span)
                : 0.0f;
  phase_ring(progress, s.playing);

  // Gear + SETTINGS at the top, in place of the old device-name line.
  const int label_w = text_w("SETTINGS", 1);
  const int gear_px = 7 * 2;
  const int group_w = gear_px + 6 + label_w;
  const int gx = kCx - group_w / 2;
  draw_gear(gx, 14, 2, kMuted);
  text(gx + gear_px + 6, 15, "SETTINGS", 1, kMuted);

  // Hero tempo. Integer nudges land on whole BPM, so drop the ".0" and
  // render the number larger (scale 6) for a glance read; only a genuinely
  // fractional Link tempo keeps the decimal at the smaller size.
  const bool whole = (s.milli_bpm % 1000u) == 0;
  char big[8];
  const char* bpm_str = s.bpm;
  int bpm_scale = 4;
  if (whole) {
    std::snprintf(big, sizeof(big), "%u",
                  static_cast<unsigned>(s.milli_bpm / 1000u));
    bpm_str = big;
    bpm_scale = 6;
  }
  const int bpm_h = 7 * bpm_scale;
  text_cx(kCx, kCy - bpm_h / 2 - 4, bpm_str, bpm_scale, kInk);
  text_cx(kCx, kCy + bpm_h / 2 + 2, "BPM", 1, kMuted);

  // Peer count under the tempo block.
  char peers[12];
  std::snprintf(peers, sizeof(peers), "%u LINK",
                static_cast<unsigned>(s.peers));
  text_cx(kCx, kCy + bpm_h / 2 + 18, peers, 1, s.peers ? kNeon : kNeonDim);

  // Transport chip near the bottom of the dial.
  const int chip_w = 96;
  const int chip_h = 34;
  const int chip_x = kCx - chip_w / 2;
  const int chip_y = kH - 74;
  fill(chip_x, chip_y, chip_w, chip_h, s.playing ? kHot : kRingDim);
  text_cx(kCx, chip_y + (chip_h - 14) / 2, s.playing ? "STOP" : "RUN", 2,
          s.playing ? kInk : kMuted);
}

// ---- Settings: apply edited config, navigation helpers -----------------

void apply_backlight() {
  halesp::lcd_gc9a01_backlight(g_cfg.display_brightness != 0);
}

void commit_menu() {
  if (!g_menu.take_dirty()) {
    return;
  }
  neon::Config live = neon_config();
  live.engine = g_cfg.engine;
  live.quantum_beats = g_cfg.quantum_beats;
  live.clock_source = g_cfg.clock_source;
  live.clock_in_ppqn = g_cfg.clock_in_ppqn;
  live.ble_enabled = g_cfg.ble_enabled;
  live.midi_clock_out = g_cfg.midi_clock_out;
  live.midi = g_cfg.midi;
  live.midi_nudge_us = g_cfg.midi_nudge_us;
  live.start_stop_sync = g_cfg.start_stop_sync;
  live.display_brightness = g_cfg.display_brightness;
  live.big_beat_display = g_cfg.big_beat_display;
  live.beat_style = g_cfg.beat_style;
  live.audio = g_cfg.audio;
  neon_config_apply(live);
  g_cfg = live;
  apply_backlight();
}

void follow_cursor() {
  const int cur = g_menu.cursor();
  if (cur < g_scroll) {
    g_scroll = cur;
  } else if (cur >= g_scroll + kRows) {
    g_scroll = cur - kRows + 1;
  }
  if (g_scroll < 0) {
    g_scroll = 0;
  }
}

void open_settings() {
  g_cfg = neon_config();
  g_menu.go_section(neon::MenuModel::Screen::kMenu);
  g_scroll = 0;
  g_settings = true;
  g_wifi = Wifi::kOff;
  halesp::encoder_clear_press();
}

void close_settings() {
  commit_menu();
  g_menu.go_home();
  g_settings = false;
  g_wifi = Wifi::kOff;
  g_scroll = 0;
}

void settings_back() {
  using S = neon::MenuModel::Screen;
  const S scr = g_menu.screen();
  if (scr == S::kMenu) {
    close_settings();
    return;
  }
  if (scr == S::kOutputEdit) {
    const int out = g_menu.output_index();
    g_menu.go_section(S::kOutputs);
    g_menu.set_cursor(out);
  } else {
    g_menu.go_section(S::kMenu);
  }
  g_scroll = 0;
}

// Menu (section list) row → destination.
void menu_activate(int row) {
  using S = neon::MenuModel::Screen;
  switch (row) {
    case 0:  // LIVE
    case 6:  // BACK
      close_settings();
      return;
    case 1:
      g_menu.go_section(S::kOutputs);
      break;
    case 2:
      g_menu.go_section(S::kNetwork);
      break;
    case 3:
      g_menu.go_section(S::kMidi);
      break;
    case 4:
      g_menu.go_section(S::kAudio);
      break;
    case 5:
      g_menu.go_section(S::kSystem);
      break;
    default:
      return;
  }
  g_scroll = 0;
}

// Activate a row on whichever settings screen is showing.
void settings_activate(int row) {
  using S = neon::MenuModel::Screen;
  const S scr = g_menu.screen();
  if (scr == S::kMenu) {
    menu_activate(row);
    return;
  }
  if (scr == S::kNetwork) {
    return;  // read-only
  }
  if (scr == S::kOutputs) {
    g_menu.set_output_index(row);
    g_menu.go_section(S::kOutputEdit);
    g_scroll = 0;
    return;
  }
  if (scr == S::kSystem && row == neon::MenuModel::kSystemRebootItem) {
    g_menu.go_section(S::kConfirm);
    return;
  }
  if (scr == S::kSystem && row == neon::MenuModel::kSystemVersionItem) {
    return;
  }
  g_menu.set_cursor(row);
  char val[24] = {};
  g_menu.item_value(row, val, sizeof(val));
  // Toggle-like rows read nicer if a tap turns them off from ON/LEAD.
  g_menu.nudge_value((std::strcmp(val, "ON") == 0 ||
                      std::strcmp(val, "LEAD") == 0)
                         ? -1
                         : 1);
  commit_menu();
}

void confirm_choice(bool yes) {
  if (yes) {
    ESP_LOGI(kTag, "reboot from settings");
    neon_config_flush_now();
    esp_restart();
  }
  g_menu.go_section(neon::MenuModel::Screen::kSystem);
  g_menu.set_cursor(neon::MenuModel::kSystemRebootItem);
  g_scroll = 0;
}

// ---- Settings rendering ------------------------------------------------

void paint_back_chip(bool exit_label) {
  const int w = 120;
  fill(kCx - w / 2, kBackY0, w, kBackH, kSurface);
  text_cx(kCx, kBackY0 + (kBackH - 7) / 2, exit_label ? "CLOSE" : "< BACK", 1,
          kNeon);
}

void paint_network(const Snap& s) {
  const char* mode = s.provisioned ? (s.wifi_up ? "WIFI" : "JOINING")
                                   : "SETUP AP";
  int y = kListY0 + 4;
  text(40, y, "MODE", 1, kMuted);
  text(150, y, mode, 1, kInk);
  y += 30;
  text(40, y, "IP", 1, kMuted);
  text(150, y, s.ip[0] ? s.ip : "-", 1, kInk);
  y += 30;
  char pc[12];
  std::snprintf(pc, sizeof(pc), "%u", static_cast<unsigned>(s.peers));
  text(40, y, "PEERS", 1, kMuted);
  text(150, y, pc, 1, kInk);
  // Tappable "scan & join" chip (also fired by an encoder click here).
  const int cw = 150;
  fill(kCx - cw / 2, kBackY0 - 40, cw, 30, kSurface2);
  text_cx(kCx, kBackY0 - 40 + (30 - 7) / 2, "SCAN & JOIN", 1, kNeon);
}

constexpr int kNetScanY0 = kBackY0 - 40;
constexpr int kNetScanH = 30;

void paint_confirm() {
  text_cx(kCx, kListY0 + 6, "REBOOT?", 2, kInk);
  text_cx(kCx, kListY0 + 34, "clock stops", 1, kMuted);
  const int bw = 70;
  const int bh = 40;
  const int by = kCy + 20;
  fill(kCx - 8 - bw, by, bw, bh, kSurface);
  text_cx(kCx - 8 - bw / 2, by + (bh - 14) / 2, "NO", 2, kInk);
  fill(kCx + 8, by, bw, bh, kHot);
  text_cx(kCx + 8 + bw / 2, by + (bh - 14) / 2, "YES", 2, kInk);
}

void paint_settings(const Snap& s) {
  using S = neon::MenuModel::Screen;
  fill(0, 0, kW, kH, kBg);

  const S scr = g_menu.screen();
  text_cx(kCx, 12, g_menu.screen_title(), 2, kNeon);
  fill(kCx - 70, 32, 140, 2, kNeonDim);

  if (scr == S::kConfirm) {
    paint_confirm();
    return;
  }
  if (scr == S::kNetwork) {
    paint_network(s);
    paint_back_chip(false);
    return;
  }

  const int n = g_menu.item_count();
  follow_cursor();
  for (int i = 0; i < kRows; ++i) {
    const int idx = g_scroll + i;
    if (idx >= n) {
      break;
    }
    const int y = kListY0 + i * kRowH;
    const bool cur = idx == g_menu.cursor();
    if (cur) {
      fill(28, y, kW - 56, kRowH - 4, kSurface2);
    }
    text(38, y + (kRowH - 4 - 7) / 2, g_menu.item_label(idx), 1,
         cur ? kInk : kMuted);
    char val[40] = {};
    if (scr == S::kSystem && idx == neon::MenuModel::kSystemVersionItem) {
      std::snprintf(val, sizeof(val), "%s", s.firmware);
    } else {
      g_menu.item_value(idx, val, sizeof(val));
    }
    text(202 - text_w(val, 1), y + (kRowH - 4 - 7) / 2, val, 1, kNeon);
  }

  // Scroll hints when the list runs past the window.
  if (g_scroll > 0) {
    text_cx(kCx, kListY0 - 10, "\x18", 1, kNeonDim);  // up
  }
  if (g_scroll + kRows < n) {
    text_cx(kCx, kBackY0 - 12, "\x19", 1, kNeonDim);  // down
  }

  paint_back_chip(scr == S::kMenu);
}

// ---- WiFi join flow ----------------------------------------------------

void wifi_connect() {
  neon::Config cfg = neon_config();
  std::snprintf(cfg.wifi[0].ssid, sizeof(cfg.wifi[0].ssid), "%s", g_join_ssid);
  std::snprintf(cfg.wifi[0].pass, sizeof(cfg.wifi[0].pass), "%s",
                g_join_open ? "" : g_pass);
  cfg.wifi[0].hidden = 0;
  neon_config_apply(cfg);
  neon_wifi_apply_credentials();
  g_connect_us = esp_timer_get_time();
  g_connected_us = 0;
  g_wifi = Wifi::kConnecting;
  ESP_LOGI(kTag, "joining \"%s\" (%s)", g_join_ssid,
           g_join_open ? "open" : "wpa");
}

void wifi_list_activate(int sel) {
  if (sel < 0 || sel >= g_ap_count) {
    return;
  }
  std::snprintf(g_join_ssid, sizeof(g_join_ssid), "%s", g_aps[sel].ssid);
  g_join_open = g_aps[sel].open != 0;
  if (g_join_open) {
    wifi_connect();
    return;
  }
  g_pass_len = 0;
  g_pass[0] = '\0';
  g_char_idx = 0;
  g_wifi = Wifi::kPass;
}

void wifi_pass_click() {
  if (g_char_idx == kSlotOk) {
    wifi_connect();
    return;
  }
  if (g_char_idx == kSlotDel) {
    if (g_pass_len > 0) {
      g_pass[--g_pass_len] = '\0';
    }
    return;
  }
  if (g_pass_len < static_cast<int>(sizeof(g_pass)) - 1) {
    g_pass[g_pass_len++] = kCharset[g_char_idx];
    g_pass[g_pass_len] = '\0';
  }
}

// Paint one frame of feedback, run the blocking scan, land on the list.
void wifi_start_scan() {
  fill(0, 0, kW, kH, kBg);
  text_cx(kCx, kCy - 8, "SCANNING", 2, kNeon);
  halesp::lcd_gc9a01_blit(g_fb, 0, 0, kW, kH);
  g_ap_count = neon_wifi_scan(g_aps, 16);
  if (g_ap_count < 0) {
    g_ap_count = 0;
  }
  g_ap_sel = 0;
  g_ap_scroll = 0;
  g_wifi = Wifi::kList;
  ESP_LOGI(kTag, "scan: %d networks", g_ap_count);
}

void paint_wifi_list() {
  fill(0, 0, kW, kH, kBg);
  text_cx(kCx, 12, "WIFI", 2, kNeon);
  fill(kCx - 70, 32, 140, 2, kNeonDim);

  if (g_ap_count == 0) {
    text_cx(kCx, kCy - 8, "NO NETWORKS", 1, kMuted);
    text_cx(kCx, kCy + 8, "click to rescan", 1, kNeonDim);
    paint_back_chip(false);
    return;
  }

  if (g_ap_sel < g_ap_scroll) {
    g_ap_scroll = g_ap_sel;
  } else if (g_ap_sel >= g_ap_scroll + kRows) {
    g_ap_scroll = g_ap_sel - kRows + 1;
  }
  for (int i = 0; i < kRows; ++i) {
    const int idx = g_ap_scroll + i;
    if (idx >= g_ap_count) {
      break;
    }
    const int y = kListY0 + i * kRowH;
    const bool cur = idx == g_ap_sel;
    if (cur) {
      fill(28, y, kW - 56, kRowH - 4, kSurface2);
    }
    char ssid[24];
    clip_str(ssid, sizeof(ssid), g_aps[idx].ssid, 22);
    text(38, y + (kRowH - 4 - 7) / 2, ssid, 1, cur ? kInk : kMuted);
    // Lock glyph for secured networks.
    if (g_aps[idx].open == 0) {
      text(190, y + (kRowH - 4 - 7) / 2, "\x07", 1, kNeon);
    }
  }
  if (g_ap_scroll > 0) {
    text_cx(kCx, kListY0 - 10, "\x18", 1, kNeonDim);
  }
  if (g_ap_scroll + kRows < g_ap_count) {
    text_cx(kCx, kBackY0 - 12, "\x19", 1, kNeonDim);
  }
  paint_back_chip(false);
}

void paint_wifi_pass() {
  fill(0, 0, kW, kH, kBg);
  char title[26];
  clip_str(title, sizeof(title), g_join_ssid, 24);
  text_cx(kCx, 14, title, 1, kMuted);

  // Entered password so far (plain, so a typo is visible), last 15 chars.
  const char* shown = g_pass;
  if (g_pass_len > 15) {
    shown = g_pass + (g_pass_len - 15);
  }
  char line[18];
  clip_str(line, sizeof(line) - 1, shown, 15);
  const int ln = static_cast<int>(std::strlen(line));
  line[ln] = '_';
  line[ln + 1] = '\0';
  text_cx(kCx, 46, g_pass_len ? line : "_", 2, kInk);

  // The picker: current slot big in the middle, neighbours faded.
  auto slot_label = [](int idx, char* buf) {
    if (idx == kSlotDel) {
      std::snprintf(buf, 4, "DEL");
    } else if (idx == kSlotOk) {
      std::snprintf(buf, 4, "OK");
    } else {
      buf[0] = kCharset[((idx % kCharsetLen) + kCharsetLen) % kCharsetLen];
      buf[1] = '\0';
    }
  };
  const int cy = kCy + 16;
  for (int off = -2; off <= 2; ++off) {
    int idx = ((g_char_idx + off) % kSlotCount + kSlotCount) % kSlotCount;
    char buf[4];
    slot_label(idx, buf);
    const int scale = off == 0 ? 4 : 2;
    const uint16_t col = off == 0 ? kNeon : kNeonDim;
    const int x = kCx + off * 34;
    text(x - text_w(buf, scale) / 2, cy - 7 * scale / 2, buf, scale, col);
  }

  text_cx(kCx, kH - 40, "turn pick . click add", 1, kNeonDim);
  text_cx(kCx, kH - 26, "hold = back", 1, kNeonDim);
}

// Once the STA has an IP, persist the credential and drop back to the home
// screen after a short confirmation so the scan chip can't be re-triggered
// by accident. Returns true when it has taken over navigation.
bool wifi_tick() {
  if (g_wifi != Wifi::kConnecting) {
    return false;
  }
  if (!neon_wifi_sta_got_ip()) {
    return false;
  }
  if (g_connected_us == 0) {
    g_connected_us = esp_timer_get_time();
    neon_config_flush_now();  // survive a power cycle immediately
    ESP_LOGI(kTag, "joined; credentials saved");
  }
  if (esp_timer_get_time() - g_connected_us > 1200000) {
    close_settings();  // g_settings=false, g_wifi=kOff → live home screen
    return true;
  }
  return false;
}

void paint_wifi_connecting() {
  fill(0, 0, kW, kH, kBg);
  const bool up = neon_wifi_sta_got_ip();
  const int64_t dt = esp_timer_get_time() - g_connect_us;
  if (up) {
    text_cx(kCx, kCy - 20, "CONNECTED", 2, kNeon);
    text_cx(kCx, kCy + 8, g_join_ssid, 1, kInk);
  } else if (dt > 20000000) {
    text_cx(kCx, kCy - 20, "NO CONNECT", 2, kHot);
    text_cx(kCx, kCy + 8, "check password", 1, kMuted);
    text_cx(kCx, kH - 40, "hold = back", 1, kNeonDim);
  } else {
    text_cx(kCx, kCy - 20, "CONNECTING", 2, kNeon);
    const int dots = static_cast<int>((dt / 400000) % 4);
    char d[5] = "....";
    d[dots] = '\0';
    text_cx(kCx, kCy + 8, d, 2, kInk);
  }
}

void paint_wifi() {
  switch (g_wifi) {
    case Wifi::kList:
      paint_wifi_list();
      break;
    case Wifi::kPass:
      paint_wifi_pass();
      break;
    case Wifi::kConnecting:
      paint_wifi_connecting();
      break;
    default:
      break;
  }
}

void wifi_tap(int x, int y) {
  switch (g_wifi) {
    case Wifi::kList:
      if (in_rect(x, y, kCx - 60, kBackY0, 120, kBackH)) {
        g_wifi = Wifi::kOff;
        return;
      }
      if (g_ap_count == 0) {
        wifi_start_scan();
        return;
      }
      if (y >= kListY0 && y < kListY0 + kRows * kRowH) {
        const int idx = g_ap_scroll + (y - kListY0) / kRowH;
        if (idx >= 0 && idx < g_ap_count) {
          g_ap_sel = idx;
          wifi_list_activate(idx);
        }
      }
      return;
    case Wifi::kPass:
      // Bottom third: left = DEL, centre = add current, right = OK.
      if (y > kCy + 40) {
        if (x < kW / 3) {
          g_char_idx = kSlotDel;
          wifi_pass_click();
        } else if (x > 2 * kW / 3) {
          wifi_connect();
        } else {
          wifi_pass_click();
        }
      }
      return;
    case Wifi::kConnecting:
      if (neon_wifi_sta_got_ip()) {
        close_settings();  // straight to the home screen
      }
      return;
    default:
      break;
  }
}

void wifi_encoder(int detents, halesp::EncoderPress press) {
  switch (g_wifi) {
    case Wifi::kList:
      if (detents != 0 && g_ap_count > 0) {
        g_ap_sel += detents;
        if (g_ap_sel < 0) {
          g_ap_sel = 0;
        }
        if (g_ap_sel >= g_ap_count) {
          g_ap_sel = g_ap_count - 1;
        }
      }
      if (press == halesp::EncoderPress::kShort) {
        if (g_ap_count == 0) {
          wifi_start_scan();
        } else {
          wifi_list_activate(g_ap_sel);
        }
      } else if (press == halesp::EncoderPress::kLong) {
        g_wifi = Wifi::kOff;
      }
      return;
    case Wifi::kPass:
      if (detents != 0) {
        g_char_idx =
            ((g_char_idx + detents) % kSlotCount + kSlotCount) % kSlotCount;
      }
      if (press == halesp::EncoderPress::kShort) {
        wifi_pass_click();
      } else if (press == halesp::EncoderPress::kLong) {
        g_wifi = Wifi::kList;
      }
      return;
    case Wifi::kConnecting:
      if (press == halesp::EncoderPress::kShort && neon_wifi_sta_got_ip()) {
        close_settings();  // straight to the home screen
      } else if (press == halesp::EncoderPress::kLong) {
        g_wifi = Wifi::kOff;  // give up / back to the network screen
      }
      return;
    default:
      break;
  }
}

// ---- Touch hit-testing -------------------------------------------------

// Handle a tap at (x, y) on the settings screen. Returns nothing; mutates
// the model.
void settings_tap(int x, int y) {
  using S = neon::MenuModel::Screen;
  const S scr = g_menu.screen();

  if (scr == S::kConfirm) {
    const int bw = 70;
    const int bh = 40;
    const int by = kCy + 20;
    if (in_rect(x, y, kCx - 8 - bw, by, bw, bh)) {
      confirm_choice(false);
    } else if (in_rect(x, y, kCx + 8, by, bw, bh)) {
      confirm_choice(true);
    }
    return;
  }

  // Back / close chip at the bottom.
  if (in_rect(x, y, kCx - 60, kBackY0, 120, kBackH)) {
    settings_back();
    return;
  }

  if (scr == S::kNetwork) {
    if (in_rect(x, y, kCx - 75, kNetScanY0, 150, kNetScanH)) {
      wifi_start_scan();
    }
    return;
  }

  // A row in the list window.
  if (y >= kListY0 && y < kListY0 + kRows * kRowH) {
    const int i = (y - kListY0) / kRowH;
    const int idx = g_scroll + i;
    if (idx >= 0 && idx < g_menu.item_count()) {
      settings_activate(idx);
    }
  }
}

// ---- Input glue --------------------------------------------------------

void push_tempo(int delta) {
  ControlCommand cmd{};
  cmd.kind = ControlCommand::Kind::kNudgeTempo;
  cmd.arg = delta;
  control_queue_push(cmd);
}

void push_kind(ControlCommand::Kind kind) {
  ControlCommand cmd{};
  cmd.kind = kind;
  control_queue_push(cmd);
}

// Debounced single-touch tap: fire once on the press edge, ignore drags.
bool poll_tap(int* tx, int* ty) {
  static bool was_down = false;
  int x = 0;
  int y = 0;
  const bool down = halesp::cst816_poll(&x, &y);
  bool tapped = false;
  if (down && !was_down) {
    tapped = true;
    *tx = x;
    *ty = y;
  }
  was_down = down;
  return tapped;
}

void handle_encoder() {
  // Encoder A/B read CCW-positive on this board; negate so CW is "forward".
  const int detents = -halesp::encoder_take_detents();
  const halesp::EncoderPress press = halesp::encoder_take_press();

  if (!g_settings) {
    if (detents != 0) {
      push_tempo(detents);
    }
    if (press == halesp::EncoderPress::kShort) {
      push_kind(ControlCommand::Kind::kToggle);
    } else if (press == halesp::EncoderPress::kLong) {
      push_kind(ControlCommand::Kind::kTapTempo);
    }
    return;
  }

  // The WiFi join flow layers on top of the Network screen.
  if (g_wifi != Wifi::kOff) {
    wifi_encoder(detents, press);
    return;
  }

  // On the Network screen a click starts the scan-and-join flow.
  if (g_menu.screen() == neon::MenuModel::Screen::kNetwork) {
    if (press == halesp::EncoderPress::kShort) {
      wifi_start_scan();
    } else if (press == halesp::EncoderPress::kLong) {
      settings_back();
    }
    return;
  }

  // In settings: rotate scrolls / edits, short click activates, long backs.
  if (detents != 0) {
    g_menu.on_rotate(detents);
    follow_cursor();
  }
  if (press == halesp::EncoderPress::kShort) {
    if (g_menu.screen() == neon::MenuModel::Screen::kConfirm) {
      confirm_choice(g_menu.confirm_yes());
    } else {
      settings_activate(g_menu.cursor());
    }
  } else if (press == halesp::EncoderPress::kLong) {
    settings_back();
  }
}

void matouch_task(void*) {
  if (!halesp::lcd_gc9a01_init()) {
    ESP_LOGE(kTag, "GC9A01 init failed");
    vTaskDelete(nullptr);
    return;
  }
  const bool enc = halesp::encoder_init(kPinEncA, kPinEncB, kPinEncSw);
  // The MaTouch bezel clicks every half quadrature cycle (2 counts/detent).
  halesp::encoder_set_counts_per_detent(2);
  ESP_LOGI(kTag, "encoder %s (a=%d b=%d sw=%d)", enc ? "ready" : "absent",
           kPinEncA, kPinEncB, kPinEncSw);
  const bool touch = halesp::cst816_init(kPinI2cSda, kPinI2cScl, 18);
  ESP_LOGI(kTag, "touch %s", touch ? "ready" : "absent");

  g_fb = static_cast<uint16_t*>(heap_caps_malloc(
      static_cast<size_t>(kW) * kH * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_fb == nullptr) {
    ESP_LOGW(kTag, "no PSRAM fb; falling back to internal RAM");
    g_fb = static_cast<uint16_t*>(heap_caps_malloc(
        static_cast<size_t>(kW) * kH * sizeof(uint16_t), MALLOC_CAP_8BIT));
  }
  if (g_fb == nullptr) {
    ESP_LOGE(kTag, "no memory for %dx%d framebuffer", kW, kH);
    vTaskDelete(nullptr);
    return;
  }

  g_cfg = neon_config();

  // Splash so the panel proves itself before Link is up.
  fill(0, 0, kW, kH, kBg);
  phase_ring(0.0f, false);
  text_cx(kCx, kCy - 20, "NEON", 3, kNeon);
  text_cx(kCx, kCy + 12, "link-mat", 1, kMuted);
  halesp::lcd_gc9a01_blit(g_fb, 0, 0, kW, kH);
  halesp::lcd_gc9a01_backlight(true);
  vTaskDelay(pdMS_TO_TICKS(700));

  for (;;) {
    handle_encoder();

    int tx = 0;
    int ty = 0;
    if (poll_tap(&tx, &ty)) {
      if (g_settings && g_wifi != Wifi::kOff) {
        wifi_tap(tx, ty);
      } else if (g_settings) {
        settings_tap(tx, ty);
      } else if (in_rect(tx, ty, kGearZoneX0, kGearZoneY0,
                         kGearZoneX1 - kGearZoneX0, kGearZoneY1 - kGearZoneY0)) {
        open_settings();
        ESP_LOGI(kTag, "touch: open settings");
      }
    }

    wifi_tick();  // auto-save + return home once the join gets an IP

    const Snap s = snapshot();
    if (g_settings && g_wifi != Wifi::kOff) {
      paint_wifi();
    } else if (g_settings) {
      paint_settings(s);
    } else {
      paint_live(s);
    }
    halesp::lcd_gc9a01_blit(g_fb, 0, 0, kW, kH);
    vTaskDelay(pdMS_TO_TICKS(40));  // ~25 fps
  }
}

}  // namespace

void neon_start_matouch_service() {
  xTaskCreatePinnedToCore(matouch_task, "matouch", 8192, nullptr, 3, nullptr,
                          kNeonCoreApp);
}

#else

void neon_start_matouch_service() {}

#endif  // CONFIG_NEON_BOARD_LINKSYNC_MATOUCH
