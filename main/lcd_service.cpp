#include "sdkconfig.h"
#include "tasks.h"

#include "app_state/audio_bus.h"
#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "board_mac.h"
#include "board_pins.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/lcd_rgb.hpp"
#include "neon/fixed_math.hpp"
#include "neon/gfx/font5x7.hpp"
#include "neon/timeline.hpp"
#include "neon/config/model.hpp"
#include "neon/transport.hpp"
#include "neon/ui/hero_font_gen.hpp"
#include "neon/ui/menu_model.hpp"
#include "neon/ui/idle_dimmer.hpp"
#include "neon/ui/theme_gen.hpp"
#include "netman/net_manager.h"
#include "provision.h"
#include "wifi.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD || CONFIG_NEON_BOARD_LINKSYNC_TAB5

namespace {

const char* kTag = "lcd";

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t c = static_cast<uint16_t>(((r & 0xf8) << 8) |
                                           ((g & 0xfc) << 3) | (b >> 3));
  if (halesp::kLcdSwapBytes) {
    return static_cast<uint16_t>((c << 8) | (c >> 8));
  }
  return c;
}

// DESIGN_SYSTEM.md Void neutrals, with the live accent taken from
// Config::color_theme. Neon is an accent, not a fill — the previous
// teal-on-cyan plates were why the glass read pastel and busy.
struct Pal {
  uint16_t bg, surface, surface2, border, ink, muted, neon, neon_dim, hot,
      hero, danger;
};
Pal g_pal{};

uint16_t hex_rgb(uint32_t h) {
  return rgb(static_cast<uint8_t>((h >> 16) & 0xff),
             static_cast<uint8_t>((h >> 8) & 0xff),
             static_cast<uint8_t>(h & 0xff));
}

void refresh_pal(neon::ColorTheme t) {
  uint32_t bg = 0x0B0C0F, surface = 0x14161A, surface2 = 0x1C1F26,
           border = 0x2A2E38, ink = 0xE8EAED, muted = 0x8B909A, neon = 0x00F0FF,
           neon_dim = 0x00A8B3, hot = 0xFF2D95, hero = 0x00F0FF,
           danger = 0xFF4D4D;
  switch (t) {
    case neon::ColorTheme::kTeal:
      bg = 0x0A1A1E;
      surface = 0x12262C;
      surface2 = 0x1A343C;
      border = 0x2E5860;
      ink = 0xD6E6EA;
      muted = 0x8AADB4;
      neon = 0x5ED4DC;
      neon_dim = 0x3AA0A8;
      hero = 0x7EE0E6;
      hot = 0xFF5AA8;
      break;
    case neon::ColorTheme::kPhosphor:
      bg = 0x07110A;
      surface = 0x0E1C12;
      surface2 = 0x16281A;
      border = 0x2A4A34;
      ink = 0xD4E8D6;
      muted = 0x8AAA90;
      neon = 0x3DFF9A;
      neon_dim = 0x22B86A;
      hero = 0x6CFFB0;
      hot = 0xFF4DA6;
      break;
    case neon::ColorTheme::kAmber:
      bg = 0x14100A;
      surface = 0x1E1810;
      surface2 = 0x2A2216;
      border = 0x4A3C24;
      ink = 0xF2E6D0;
      muted = 0xB09A74;
      neon = 0xFFB020;
      neon_dim = 0xC48418;
      hero = 0xFFC24A;
      hot = 0xFF4D8A;
      break;
    case neon::ColorTheme::kMagenta:
      bg = 0x140A12;
      surface = 0x1E1018;
      surface2 = 0x2A1824;
      border = 0x4A2A40;
      ink = 0xF0E0EA;
      muted = 0xB090A0;
      neon = 0xFF5AB0;
      neon_dim = 0xD04090;
      hero = 0xFF7AC4;
      hot = 0xFF2D95;
      break;
    case neon::ColorTheme::kPaper:
      bg = 0xF3EEE6;
      surface = 0xFFFBF5;
      surface2 = 0xE6DFD4;
      border = 0xC4BBAE;
      ink = 0x1A1814;
      muted = 0x5C564C;
      neon = 0x007278;
      neon_dim = 0x0A8A94;
      hero = 0x00646C;
      hot = 0xC4006A;
      danger = 0xC42828;
      break;
    default:  // Void — the design-system default
      break;
  }
  // Dark themes share Void neutrals so the chassis stays graphite and
  // only the accent hue changes. Paper keeps its light plates.
  if (t != neon::ColorTheme::kPaper) {
    bg = 0x0B0C0F;
    surface = 0x14161A;
    surface2 = 0x1C1F26;
    border = 0x2A2E38;
    ink = 0xE8EAED;
    muted = 0x8B909A;
  }
  g_pal.bg = hex_rgb(bg);
  g_pal.surface = hex_rgb(surface);
  g_pal.surface2 = hex_rgb(surface2);
  g_pal.border = hex_rgb(border);
  g_pal.ink = hex_rgb(ink);
  g_pal.muted = hex_rgb(muted);
  g_pal.neon = hex_rgb(neon);
  g_pal.neon_dim = hex_rgb(neon_dim);
  g_pal.hot = hex_rgb(hot);
  g_pal.hero = hex_rgb(hero);
  g_pal.danger = hex_rgb(danger);
}

// CrowPanel native scanout is 800×480. Portrait is a drawing-time
// rotation into that buffer (logical 480×800), same idea as the RLCD
// face. Tab5 is already a native portrait panel — no map.
bool g_portrait = false;

struct Lay {
  int kW, kH, kPad, kHeadH;
  int kTapX, kTapY, kTapW, kTapH;
  int kBtnW, kBtnH, kMinusX, kMinusY, kPlusX, kPlusY;
  int kTrX, kTrY, kTrW, kTrH;
  int kStageX, kStageY, kStageW, kStageH;
  int kBarY, kBarH, kFootY, kFootLine1, kFootLine2, kFootScale;
  int kBpmCell, kTapLabelScale, kTrScale, kBtnScale;
  int kGearS, kGearX, kGearY;
  int kSetHeadH, kSetTabH, kSetRowH, kSetCloseW, kSetNudgeW;
};
Lay g_lay{};

bool lcd_is_portrait() {
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
  const uint8_t v = neon_config().display_portrait;
  if (v == 2) {
    return false;
  }
  if (v == 1) {
    return true;
  }
  return true;  // 0 = board default (portrait)
#else
  return false;
#endif
}

void to_phys(int x, int y, int* px, int* py) {
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
  if (g_portrait) {
    *px = y;
    *py = halesp::kLcdH - 1 - x;
    return;
  }
#endif
  *px = x;
  *py = y;
}

void from_phys(int px, int py, int* x, int* y) {
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
  if (g_portrait) {
    *x = halesp::kLcdH - 1 - py;
    *y = px;
    return;
  }
#endif
  *x = px;
  *y = py;
}

void rebuild_layout() {
#if CONFIG_NEON_BOARD_LINKSYNC_TAB5
  g_portrait = false;
  g_lay = {720, 1280, 32, 64,
           32, 80, 656, 232,
           280, 150, 32, 332, 408, 332,
           32, 502, 656, 170,
           32, 692, 656, 440,
           1148, 18, 1180, 1188, 1224, 3,
           6, 3, 8, 8,
           56, 632, 4,
           64, 64, 80, 80, 80};
#else
  g_portrait = lcd_is_portrait();
  const int W = g_portrait ? 480 : 800;
  const int H = g_portrait ? 800 : 480;
  const int pad = g_portrait ? 28 : 24;
  const int tap_h = g_portrait ? 280 : 156;
  const int btn_w = g_portrait ? 190 : 240;
  const int btn_h = g_portrait ? 110 : 88;
  const int tr_h = g_portrait ? 130 : 78;
  const int head = g_portrait ? 52 : 44;
  const int tap_y = head + 12;
  const int minus_y = tap_y + tap_h + (g_portrait ? 24 : 16);
  const int tr_y = minus_y + btn_h + (g_portrait ? 20 : 12);
  const int bar_y = tr_y + tr_h + (g_portrait ? 24 : 10);
  const int foot1 = bar_y + 18;
  g_lay = {W, H, pad, head,
           pad, tap_y, W - 2 * pad, tap_h,
           btn_w, btn_h, pad, minus_y, W - pad - btn_w, minus_y,
           pad, tr_y, W - 2 * pad, tr_h,
           0, 0, 0, 0,
           bar_y, g_portrait ? 14 : 10, foot1, foot1, foot1 + 22,
           2,
           g_portrait ? 8 : 7, 2, g_portrait ? 7 : 6, g_portrait ? 7 : 6,
           44, W - pad - 44, 6,
           48, 52, 56, 56, 56};
#endif
}

void fill(uint16_t* fb, int x, int y, int w, int h, uint16_t c) {
  const int LW = g_lay.kW != 0 ? g_lay.kW : halesp::kLcdW;
  const int LH = g_lay.kH != 0 ? g_lay.kH : halesp::kLcdH;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > LW) {
    w = LW - x;
  }
  if (y + h > LH) {
    h = LH - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
  if (g_portrait) {
    for (int yy = y; yy < y + h; ++yy) {
      for (int xx = x; xx < x + w; ++xx) {
        int px, py;
        to_phys(xx, yy, &px, &py);
        if (px >= 0 && py >= 0 && px < halesp::kLcdW && py < halesp::kLcdH) {
          fb[py * halesp::kLcdW + px] = c;
        }
      }
    }
    return;
  }
#endif
  for (int yy = y; yy < y + h; ++yy) {
    uint16_t* row = fb + yy * halesp::kLcdW + x;
    for (int xx = 0; xx < w; ++xx) {
      row[xx] = c;
    }
  }
}

void text(uint16_t* fb, int x, int y, const char* s, int scale, uint16_t c) {
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
          fill(fb, cx + col * scale, y + row * scale, scale, scale, c);
        }
      }
    }
    cx += 6 * scale;
  }
}

int text_width(const char* s, int scale) {
  if (s == nullptr || scale < 1) {
    return 0;
  }
  return static_cast<int>(std::strlen(s)) * 6 * scale;
}

void text_cx(uint16_t* fb, int cx, int y, const char* s, int scale,
             uint16_t c) {
  text(fb, cx - text_width(s, scale) / 2, y, s, scale, c);
}

// Draw at most max_w px of `s`. Device names, SSIDs and passwords are
// user-sized; unclipped they ran into whatever was right of them.
void text_clip(uint16_t* fb, int x, int y, const char* s, int scale,
               uint16_t c, int max_w) {
  if (s == nullptr || scale < 1) {
    return;
  }
  char buf[64];
  int n = max_w / (6 * scale);
  if (n <= 0) {
    return;
  }
  if (n > static_cast<int>(sizeof(buf)) - 1) {
    n = sizeof(buf) - 1;
  }
  int i = 0;
  for (; i < n && s[i] != '\0'; ++i) {
    buf[i] = s[i];
  }
  buf[i] = '\0';
  text(fb, x, y, buf, scale, c);
}

// ---- hero numerals (design/fonts/hero.json seven-segment) --------------
// The BPM readout and the giant beat digit use these instead of scaled
// 5×7 — at panel scale the body font reads as chunky blocks.

const neon::ui::HeroGlyph* hero_glyph(char ch) {
  for (const auto& g : neon::ui::kHeroGlyphs) {
    if (g.ch == ch) {
      return &g;
    }
  }
  return nullptr;
}

int hero_width(const char* s, int cell) {
  int w = 0;
  for (const char* p = s; *p != '\0'; ++p) {
    const auto* g = hero_glyph(*p);
    if (g == nullptr) {
      continue;
    }
    if (w != 0) {
      w += neon::ui::kHeroTracking * cell;
    }
    w += g->width * cell;
  }
  return w;
}

void hero_text(uint16_t* fb, int x, int y, const char* s, int cell,
               uint16_t c) {
  int cx = x;
  bool first = true;
  for (const char* p = s; *p != '\0'; ++p) {
    const auto* g = hero_glyph(*p);
    if (g == nullptr) {
      continue;
    }
    if (!first) {
      cx += neon::ui::kHeroTracking * cell;
    }
    first = false;
    for (int col = 0; col < g->width; ++col) {
      const uint32_t bits = g->cols[col];
      int row = 0;
      while (row < neon::ui::kHeroHeight) {
        if ((bits >> row) & 1u) {
          int run = row + 1;
          while (run < neon::ui::kHeroHeight && ((bits >> run) & 1u)) {
            ++run;
          }
          fill(fb, cx + col * cell, y + row * cell, cell, (run - row) * cell,
               c);
          row = run;
        } else {
          ++row;
        }
      }
    }
    cx += g->width * cell;
  }
}

void frame(uint16_t* fb, int x, int y, int w, int h, uint16_t c, int t) {
  fill(fb, x, y, w, t, c);
  fill(fb, x, y + h - t, w, t, c);
  fill(fb, x, y, t, h, c);
  fill(fb, x + w - t, y, t, h, c);
}

// Cut-corner rect so fat buttons don't look like raw slabs.
void fill_cut(uint16_t* fb, int x, int y, int w, int h, uint16_t c) {
  fill(fb, x + 3, y, w - 6, h, c);
  fill(fb, x, y + 3, w, h - 6, c);
  fill(fb, x + 1, y + 1, w - 2, h - 2, c);
}

struct Snap {
  uint32_t milli_bpm = 0;
  uint32_t phase = 0;
  uint32_t quantum = 4;
  uint32_t beat = 1;
  uint32_t in_beat = 0;  // 0..999 milli-beats inside the current beat
  uint32_t peers = 0;
  bool playing = false;
  uint8_t big_beat = 1;
  uint8_t beat_style = 0;
  bool provisioned = false;
  bool wifi_up = false;
  bool setup_ap = false;
  bool show_ap = false;
  char ap_ssid[33] = {};
  char name[32] = {};
  char bpm[24] = {};
  char ip[16] = {};
  char firmware[32] = {};
  char wifi_ssid[33] = {};
  FollowSource follow_source = FollowSource::kNone;
  uint8_t follow_lock = 0;
  uint32_t follow_mbpm = 0;
};

Snap snapshot() {
  Snap s{};
  const neon::Config& cfg = neon_config();
  neon::TimelineSnapshot tl{};
  timeline_bus().read(tl);
  const int64_t now = esp_timer_get_time();
  // Same conversion as OLED / telemetry. Hardcoding 500000 µs (120 BPM)
  // hid live Link tempo whenever the first snapshot was late.
  const uint64_t mpb_us = (tl.tempo_mpb_q32 + (1ull << 31)) >> 32;
  if (mpb_us != 0) {
    s.milli_bpm = neon::milli_bpm_from_mpb_us(mpb_us);
  } else if (cfg.tempo_milli_bpm != 0) {
    s.milli_bpm = cfg.tempo_milli_bpm;
  } else {
    s.milli_bpm = 120000;
  }
  s.phase = neon::phase_milli_beats(tl, now);
  s.quantum = tl.quantum_beats != 0 ? tl.quantum_beats : 4;
  s.beat = neon::beat_number(s.phase, s.quantum);
  s.in_beat = s.phase % 1000u;
  s.playing = tl.playing != 0;
  s.big_beat = cfg.big_beat_display;
  s.beat_style = static_cast<uint8_t>(cfg.beat_style);
  s.peers = tl.num_peers;
  s.provisioned = neon_wifi_has_credentials();
  s.wifi_up = neon_wifi_sta_got_ip();
  s.setup_ap = netman::ap_is_up();
  s.show_ap = (s.setup_ap || !s.provisioned) && cfg.ap_pass[0] != '\0';
  const char* name = cfg.device_name[0] ? cfg.device_name : "link-lcd";
  std::snprintf(s.name, sizeof(s.name), "%s", name);
  if (s.setup_ap && netman::ap_ssid()[0] != '\0') {
    std::snprintf(s.ap_ssid, sizeof(s.ap_ssid), "%s", netman::ap_ssid());
  } else {
    uint8_t mac[6] = {};
    neon_read_unit_mac(mac);
    neon::ap_ssid_for(cfg, mac, s.ap_ssid, sizeof(s.ap_ssid));
  }
  std::snprintf(s.bpm, sizeof(s.bpm), "%u",
                static_cast<unsigned>((s.milli_bpm + 500u) / 1000u));
  netman::primary_ip(s.ip, sizeof(s.ip));
  const char* ssid = neon_wifi_current_ssid();
  std::snprintf(s.wifi_ssid, sizeof(s.wifi_ssid), "%s",
                ssid != nullptr ? ssid : "");
  const esp_app_desc_t* desc = esp_app_get_description();
  std::snprintf(s.firmware, sizeof(s.firmware), "%s",
                desc != nullptr ? desc->version : "unknown");
  s.follow_source = app_status_follow_source();
  neon::FollowStatus fst;
  follow_status_bus().read(fst);
  s.follow_lock = fst.lock;
  s.follow_mbpm = fst.published_mbpm != 0 ? fst.published_mbpm : fst.mbpm;
  static uint32_t last_mbpm = 0;
  if (s.milli_bpm != last_mbpm) {
    ESP_LOGI(kTag, "bpm %s (tl_q32=%llu cfg=%u)", s.bpm,
             static_cast<unsigned long long>(tl.tempo_mpb_q32),
             static_cast<unsigned>(cfg.tempo_milli_bpm));
    last_mbpm = s.milli_bpm;
  }
  return s;
}

enum class Hit : uint8_t {
  kNone,
  kMinus,
  kPlus,
  kTransport,
  kTap,
  kGear,
  kClose,
  kBack,
  kTab,
  kRow,
  kRowMinus,
  kRowPlus,
  kConfirmYes,
  kConfirmNo,
  kWifiBack,
  kWifiAp,
  kWifiScan,
  kKbKey,
  kKbShift,
  kKbSym,
  kKbBksp,
  kKbJoin,
};

struct Touch {
  Hit hit = Hit::kNone;
  int arg = 0;
};

// Layout lives in g_lay (rebuild_layout): Tab5 keeps the native portrait
// face with a beat stage; CrowPanel is full-width BPM / ± / transport
// and software-rotates for portrait.

bool in_rect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

neon::Config g_cfg;
neon::MenuModel g_menu(&g_cfg);
bool g_settings = false;

// Idle display power (docs/SOLAROS_PORTS_HANDOFF.md §3): dims after
// display_dim_s without a touch, blanks a stopped transport. The
// waking touch is swallowed so it cannot tap a control blind.
neon::ui::IdleDimmer g_dimmer;
bool g_swallow_touch = false;
int g_bl_applied = -1;
int g_scroll_px = 0;

enum class WifiPage : uint8_t { kOff, kScanning, kList, kKeyboard, kJoining };
volatile WifiPage g_wifi = WifiPage::kOff;
constexpr int kMaxAps = 12;
NeonWifiScanEntry g_aps[kMaxAps] = {};
int g_ap_n = 0;
int g_wifi_scroll = 0;
int64_t g_scan_started_us = 0;
bool g_scan_no_radio = false;
char g_kb_ssid[33] = {};
char g_kb_pass[65] = {};
bool g_kb_open = false;
bool g_kb_shift = false;
bool g_kb_sym = false;
int64_t g_join_us = 0;

constexpr int kTabCount = 5;
const neon::MenuModel::Screen kTabs[kTabCount] = {
    neon::MenuModel::Screen::kOutputs, neon::MenuModel::Screen::kNetwork,
    neon::MenuModel::Screen::kMidi,    neon::MenuModel::Screen::kAudio,
    neon::MenuModel::Screen::kSystem,
};
const char* kTabLabel[kTabCount] = {
    neon::ui::kTitleOutputs, neon::ui::kTitleNetwork, neon::ui::kTitleMidi,
    neon::ui::kTitleAudio,   neon::ui::kTitleSystem,
};

int tab_index_for(neon::MenuModel::Screen s) {
  using S = neon::MenuModel::Screen;
  if (s == S::kOutputEdit) {
    return 0;
  }
  if (s == S::kConfirm) {
    return 4;
  }
  for (int i = 0; i < kTabCount; ++i) {
    if (kTabs[i] == s) {
      return i;
    }
  }
  return 4;
}

int set_list_y() { return g_lay.kSetHeadH + g_lay.kSetTabH; }

int set_list_h() { return g_lay.kH - set_list_y(); }

// AUDIO: hide FOLLOW / F SENS / F PHASE when this board has no ADC.
constexpr int kAudioVisNoAdc[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
constexpr int kAudioVisNoAdcCount = 9;

const int* audio_row_map(int* count) {
  if (kPinI2sDin < 0) {
    *count = kAudioVisNoAdcCount;
    return kAudioVisNoAdc;
  }
  *count = g_menu.item_count();
  return nullptr;
}

int settings_real_index(int vis) {
  using S = neon::MenuModel::Screen;
  if (g_menu.screen() != S::kAudio) {
    return vis;
  }
  int n = 0;
  const int* map = audio_row_map(&n);
  if (map == nullptr || vis < 0 || vis >= n) {
    return vis;
  }
  return map[vis];
}

int set_row_count(const Snap& s) {
  using S = neon::MenuModel::Screen;
  const S scr = g_menu.screen();
  if (scr == S::kNetwork) {
    return s.setup_ap ? 6 : 5;
  }
  if (scr == S::kConfirm) {
    return 0;
  }
  if (scr == S::kAudio) {
    int n = 0;
    audio_row_map(&n);
    return n;
  }
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
  if (scr == S::kSystem) {
    return g_menu.item_count() + 1;  // extra SCREEN row
  }
#endif
  return g_menu.item_count();
}

int max_scroll(const Snap& s) {
  const int content = set_row_count(s) * g_lay.kSetRowH;
  const int extra = content - set_list_h();
  return extra > 0 ? extra : 0;
}

void clamp_scroll(const Snap& s) {
  const int mx = max_scroll(s);
  if (g_scroll_px < 0) {
    g_scroll_px = 0;
  }
  if (g_scroll_px > mx) {
    g_scroll_px = mx;
  }
}

void apply_backlight(uint8_t brightness) {
  // STC8 (P4LCD) / LEDC (Tab5) both take 0-100 percent.
  halesp::lcd_rgb_backlight(static_cast<uint8_t>((brightness * 100u) / 255u));
}

// Dimmer-aware brightness, written only on change (the P4LCD backlight
// is an I2C register on the STC8 expander — no per-frame writes).
void apply_effective_backlight(int64_t now_us) {
  const uint8_t base = g_settings ? g_cfg.display_brightness
                                  : neon_config().display_brightness;
  const int eff = g_dimmer.apply(base, now_us);
  if (eff != g_bl_applied) {
    apply_backlight(static_cast<uint8_t>(eff));
    g_bl_applied = eff;
  }
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
  live.display_dim_s = g_cfg.display_dim_s;
  live.display_dim_level = g_cfg.display_dim_level;
  live.audio = g_cfg.audio;
  live.audio_follow_enabled = g_cfg.audio_follow_enabled;
  live.audio_follow_phase = g_cfg.audio_follow_phase;
  live.audio_follow_sensitivity = g_cfg.audio_follow_sensitivity;
  live.audio_follow_input = g_cfg.audio_follow_input;
  live.color_theme = g_cfg.color_theme;
  live.display_portrait = g_cfg.display_portrait;
  live.ap_policy = g_cfg.ap_policy;
  neon_config_apply(live);
  g_cfg = live;
  g_bl_applied = -1;  // force a rewrite with the committed brightness
  apply_effective_backlight(esp_timer_get_time());
}

void wifi_ui_reset();  // defined with the wifi overlay

void open_settings() {
  g_cfg = neon_config();
  wifi_ui_reset();
  g_menu.go_section(neon_wifi_has_credentials()
                        ? neon::MenuModel::Screen::kMidi
                        : neon::MenuModel::Screen::kNetwork);
  g_scroll_px = 0;
  g_settings = true;
}

void close_settings() {
  commit_menu();
  wifi_ui_reset();
  g_menu.go_home();
  g_settings = false;
  g_scroll_px = 0;
}

const char* lcd_item_label(int i) {
  using S = neon::MenuModel::Screen;
  const S scr = g_menu.screen();
  if (scr == S::kMidi) {
    static const char* k[] = {"BLE MIDI", "MIDI CLOCK", "CHANNEL", "GATE",
                              "PITCH CV"};
    if (i >= 0 && i < 5) {
      return k[i];
    }
  } else if (scr == S::kSystem) {
    static const char* k[] = {
        "LATENCY",    "RESET PULSE", "CLOCK SRC",  "CLK IN",
        "GATE CLK",   "QUANTUM",     "RST EDGE",   "MIDI NUDGE",
        "START/STOP", "BRIGHTNESS",  "BEAT DISP",  "BEAT STYLE", "COLOUR",
        "IDLE DIM",   "DIM LEVEL",   "VERSION",    "REBOOT"};
    if (i >= 0 && i < 17) {
      return k[i];
    }
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
    if (i == 17) {
      return "SCREEN";
    }
#endif
  } else if (scr == S::kAudio) {
    static const char* k[] = {"AUDIO",  "METRONOME", "CLICK",   "SOUND",
                              "OUT L",  "OUT R",     "LINE IN", "PUBLISH",
                              "SUB",    "FOLLOW",    "F SENS",  "F PHASE"};
    const int real = settings_real_index(i);
    if (real >= 0 && real < 12) {
      return k[real];
    }
  }
  return g_menu.item_label(i);
}

bool row_has_nudge(int i) {
  using S = neon::MenuModel::Screen;
  const S scr = g_menu.screen();
  if (scr == S::kNetwork) {
    return i == 0;  // MODE = ap_policy
  }
  if (scr == S::kOutputs || scr == S::kConfirm) {
    return false;
  }
  if (scr == S::kSystem && (i == neon::MenuModel::kSystemVersionItem ||
                            i == neon::MenuModel::kSystemRebootItem)) {
    return false;
  }
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
  if (scr == S::kSystem && i == g_menu.item_count()) {
    return false;
  }
#endif
  return true;
}

const char* ap_policy_label(neon::ApPolicy p) {
  switch (p) {
    case neon::ApPolicy::kAlways:
      return "ALWAYS";
    case neon::ApPolicy::kOff:
      return "OFF";
    default:
      return "FALLBACK";
  }
}

void apply_ap_policy(neon::ApPolicy p) {
  g_cfg.ap_policy = p;
  neon::Config live = neon_config();
  live.ap_policy = p;
  neon_config_apply(live);
  if (p == neon::ApPolicy::kOff) {
    netman::ap_stop();
  }
  ESP_LOGI(kTag, "ap policy %s", ap_policy_label(p));
}

void cycle_ap_policy(int delta) {
  int v = static_cast<int>(g_cfg.ap_policy) + (delta > 0 ? 1 : -1);
  if (v < 0) {
    v = 2;
  }
  if (v > 2) {
    v = 0;
  }
  apply_ap_policy(static_cast<neon::ApPolicy>(v));
}

int network_scan_row(const Snap& s) { return s.setup_ap ? 5 : 4; }

void net_row(const Snap& s, int i, char* label, int lcap, char* value,
             int vcap) {
  const neon::Config& cfg = neon_config();
  label[0] = '\0';
  value[0] = '\0';
  switch (i) {
    case 0:
      std::snprintf(label, lcap, "MODE");
      std::snprintf(value, vcap, "%s", ap_policy_label(g_cfg.ap_policy));
      break;
    case 1:
      std::snprintf(label, lcap, s.setup_ap ? "AP" : "WIFI");
      std::snprintf(value, vcap, "%s",
                    s.setup_ap ? (s.ap_ssid[0] ? s.ap_ssid : "-")
                               : (s.wifi_ssid[0] ? s.wifi_ssid : "-"));
      break;
    case 2:
      std::snprintf(label, lcap, "IP");
      std::snprintf(value, vcap, "%s", s.ip[0] ? s.ip : "-");
      break;
    case 3:
      std::snprintf(label, lcap, "PEERS");
      std::snprintf(value, vcap, "%u", static_cast<unsigned>(s.peers));
      break;
    case 4:
      if (s.setup_ap) {
        std::snprintf(label, lcap, "AP PASS");
        std::snprintf(value, vcap, "%s", cfg.ap_pass[0] ? cfg.ap_pass : "-");
      } else {
        std::snprintf(label, lcap, "SCAN");
        std::snprintf(value, vcap, "JOIN A NETWORK");
      }
      break;
    default:
      std::snprintf(label, lcap, "SCAN");
      std::snprintf(value, vcap, "JOIN A NETWORK");
      break;
  }
}

void paint_btn(uint16_t* fb, int x, int y, int w, int h, const char* label,
               int scale, bool pressed, bool accent) {
  const uint16_t fill_c =
      pressed ? g_pal.neon : (accent ? g_pal.surface2 : g_pal.surface);
  fill(fb, x, y, w, h, fill_c);
  frame(fb, x, y, w, h, pressed ? g_pal.ink : g_pal.border, 1);
  text_cx(fb, x + w / 2, y + (h - 7 * scale) / 2, label, scale,
          pressed ? g_pal.bg : g_pal.ink);
}

void paint_gear(uint16_t* fb, int x, int y, int scale, uint16_t c) {
  // 7×7 cog, LSB = left.
  static const uint8_t kBits[7] = {0x14, 0x3e, 0x63, 0x55, 0x63, 0x3e, 0x14};
  for (int row = 0; row < 7; ++row) {
    const uint8_t bits = kBits[row];
    for (int col = 0; col < 7; ++col) {
      if ((bits >> col) & 1u) {
        fill(fb, x + col * scale, y + row * scale, scale, scale, c);
      }
    }
  }
}

void paint_header(uint16_t* fb, const Snap& s) {
  const int kW = g_lay.kW;
  const int kPad = g_lay.kPad;
  // 1 px playing tick — the stage carries the motion, the header stays still.
  fill(fb, 0, 0, kW, 1, s.playing ? g_pal.neon : g_pal.border);
  const int ty = (g_lay.kHeadH - 14) / 2 + 4;
  char peers[24];
  std::snprintf(peers, sizeof(peers), "%u", static_cast<unsigned>(s.peers));
  const int peers_w = text_width(peers, 3);
  const int right = g_lay.kGearX - 16;
  text(fb, right - peers_w, ty - 4, peers, 3, g_pal.neon);
  const int label_x = right - peers_w - text_width("PEERS", 2) - 10;
  text(fb, label_x, ty, "PEERS", 2, g_pal.muted);
  text_clip(fb, kPad, ty, s.name, 2, g_pal.ink, label_x - kPad - 12);
}

void paint_footer(uint16_t* fb, const Snap& s) {
  const int kW = g_lay.kW;
  const int kPad = g_lay.kPad;
  const int sc = g_lay.kFootScale;
  const int y1 = g_lay.kFootLine1;
  const int y2 = g_lay.kFootLine2;
  const neon::Config& cfg = neon_config();
  if (s.show_ap) {
    text(fb, kPad, y1, s.ap_ssid[0] ? s.ap_ssid : "LINK-LCD", sc, g_pal.ink);
    text(fb, kW - kPad - text_width("192.168.4.1", sc), y1, "192.168.4.1", sc,
         g_pal.muted);
    text(fb, kPad, y2, cfg.ap_pass, sc, g_pal.muted);
  } else if (!s.provisioned) {
    text(fb, kPad, y1, "NO WIFI", sc, g_pal.muted);
    text(fb, kPad, y2, "SETTINGS > NETWORK TO JOIN", sc, g_pal.muted);
  } else {
    const char* net = s.wifi_up ? (s.wifi_ssid[0] ? s.wifi_ssid : "-")
                                : "CONNECTING";
    text(fb, kPad, y1, net, sc, s.wifi_up ? g_pal.ink : g_pal.muted);
    if (s.ip[0]) {
      const int iw = text_width(s.ip, sc);
      text(fb, kW - kPad - iw, y1, s.ip, sc, g_pal.muted);
    }
    if (s.follow_source == FollowSource::kAudio && s.follow_lock != 0) {
      char follow[28] = {};
      if (s.follow_lock == 2) {
        std::snprintf(follow, sizeof(follow), "FOLLOW AUDIO  %u",
                      static_cast<unsigned>(s.follow_mbpm / 1000u));
      } else {
        std::snprintf(follow, sizeof(follow), "FOLLOW ...");
      }
      text(fb, kPad, y2, follow, sc, g_pal.neon);
    }
  }
}

// ---- beat stage ---------------------------------------------------------
// The "is it running" panel: bright, big, and in motion while the
// transport runs; dim and still while it is stopped. Honors SYSTEM >
// BEAT DISP / BEAT STYLE like the OLED's full-screen beat page.

void stage_dots(uint16_t* fb, int cx, int y, uint32_t q, uint32_t beat,
                bool playing) {
  if (q < 2 || q > 12) {
    return;
  }
  constexpr int kSize = 10;
  constexpr int kGap = 12;
  const int total =
      static_cast<int>(q) * kSize + (static_cast<int>(q) - 1) * kGap;
  int x = cx - total / 2;
  for (uint32_t i = 1; i <= q; ++i) {
    uint16_t c = g_pal.surface2;
    if (playing && i == beat) {
      c = beat == 1 ? g_pal.hot : g_pal.neon;
    } else if (playing && i < beat) {
      c = g_pal.neon_dim;
    }
    fill(fb, x, y, kSize, kSize, c);
    x += kSize + kGap;
  }
}

// Ink bounds of a hero string in cell units, so a seven-segment "1"
// (ink only in the right columns) centers on what is drawn, not on the
// glyph box.
void hero_ink_bounds(const char* s, int* lo, int* hi) {
  *lo = 0;
  *hi = 0;
  int at = 0;
  int min_c = 1 << 20;
  int max_c = -1;
  bool first = true;
  for (const char* p = s; *p != '\0'; ++p) {
    const auto* g = hero_glyph(*p);
    if (g == nullptr) {
      continue;
    }
    if (!first) {
      at += neon::ui::kHeroTracking;
    }
    first = false;
    for (int col = 0; col < g->width; ++col) {
      if (g->cols[col] != 0) {
        if (at + col < min_c) {
          min_c = at + col;
        }
        if (at + col > max_c) {
          max_c = at + col;
        }
      }
    }
    at += g->width;
  }
  if (max_c >= 0) {
    *lo = min_c;
    *hi = max_c + 1;
  }
}

void stage_number(uint16_t* fb, const Snap& s, int ix, int iy, int iw,
                  int ah) {
  char d[8];
  std::snprintf(d, sizeof(d), "%u", static_cast<unsigned>(s.beat));
  int ink_lo = 0;
  int ink_hi = 0;
  hero_ink_bounds(d, &ink_lo, &ink_hi);
  const int ink_w = ink_hi - ink_lo;
  int cell = ah / neon::ui::kHeroHeight;
  if (ink_w > 0 && cell * ink_w > iw) {
    cell = iw / ink_w;
  }
  if (cell < 1) {
    cell = 1;
  }
  const bool flash = s.in_beat < 140;
  if (flash) {
    fill(fb, ix, iy, iw, ah, g_pal.surface);
  }
  const int x = ix + (iw - ink_w * cell) / 2 - ink_lo * cell;
  const int y = iy + (ah - neon::ui::kHeroHeight * cell) / 2;
  hero_text(fb, x, y, d, cell, s.beat == 1 ? g_pal.hot : g_pal.neon);
}

void stage_pie(uint16_t* fb, const Snap& s, int ix, int iy, int iw, int ah) {
  const int cx = ix + iw / 2;
  const int cy = iy + ah / 2;
  int r = (iw < ah ? iw : ah) / 2 - 4;
  if (r < 12) {
    return;
  }
  const uint32_t q = s.quantum != 0 ? s.quantum : 4;
  const float frac = (static_cast<float>(s.beat - 1) +
                      static_cast<float>(s.in_beat) / 1000.0f) /
                     static_cast<float>(q);
  const float th = frac * 6.2831853f;
  // Sector test with integer cross products (12 o'clock, clockwise) —
  // atan2 per pixel would eat the frame budget at this radius.
  const int ex = static_cast<int>(std::sin(th) * 256.0f);
  const int ey = static_cast<int>(-std::cos(th) * 256.0f);
  const bool wide = frac > 0.5f;
  const int r2 = r * r;
  const int ri2 = (r - 3) * (r - 3);
  for (int py = -r; py <= r; ++py) {
    uint16_t* row = fb + (cy + py) * halesp::kLcdW + cx;
    for (int px = -r; px <= r; ++px) {
      const int d2 = px * px + py * py;
      if (d2 > r2) {
        continue;
      }
      if (d2 >= ri2) {
        row[px] = g_pal.neon_dim;
        continue;
      }
      const int ce = px * ey - py * ex;
      const bool in = wide ? !((ex * py - ey * px) >= 0 && px <= 0)
                           : (px >= 0 && ce >= 0);
      if (in) {
        row[px] = g_pal.neon;
      }
    }
  }
  fill(fb, cx - 4, cy - 4, 8, 8, g_pal.ink);
}

void stage_pendulum(uint16_t* fb, const Snap& s, int ix, int iy, int iw,
                    int ah) {
  const int px0 = ix + iw / 2;
  const int py0 = iy + 12;
  // Rod length bounded by height AND by the swing staying inside the
  // stage: sin(0.55) ≈ 0.523, plus the bob radius. The bob writes rows
  // directly, so this is a hard bound, not a cosmetic one.
  int len = ah - 44;
  const int len_x = ((iw / 2 - 20) * 100) / 53;
  if (len_x < len) {
    len = len_x;
  }
  if (len < 24) {
    return;
  }
  // Cosine swing, one tick per beat, like the OLED pendulum. `phase` is
  // continuous milli-beats so the bob is in motion every frame.
  const float beats = static_cast<float>(s.phase) / 1000.0f;
  const float theta = -0.55f * std::cos(3.14159265f * beats);
  const int bx =
      px0 + static_cast<int>(std::sin(theta) * static_cast<float>(len));
  const int by =
      py0 + static_cast<int>(std::cos(theta) * static_cast<float>(len));
  // Swing extremes so the eye has rails to track against.
  const int tx = static_cast<int>(std::sin(0.55f) * static_cast<float>(len));
  const int ty = py0 + static_cast<int>(std::cos(0.55f) *
                                        static_cast<float>(len));
  fill(fb, px0 - tx - 2, ty + 20, 4, 14, g_pal.border);
  fill(fb, px0 + tx - 2, ty + 20, 4, 14, g_pal.border);
  constexpr int kSteps = 26;
  for (int i = 0; i <= kSteps; ++i) {
    const int xi = px0 + ((bx - px0) * i) / kSteps;
    const int yi = py0 + ((by - py0) * i) / kSteps;
    fill(fb, xi - 3, yi - 3, 6, 6, g_pal.neon_dim);
  }
  fill(fb, px0 - 5, py0 - 5, 10, 10, g_pal.ink);
  constexpr int kBob = 16;
  const uint16_t bc = (s.beat == 1 && s.in_beat < 140) ? g_pal.hot : g_pal.neon;
  for (int dy = -kBob; dy <= kBob; ++dy) {
    uint16_t* row = fb + (by + dy) * halesp::kLcdW + bx;
    for (int dx = -kBob; dx <= kBob; ++dx) {
      if (dx * dx + dy * dy <= kBob * kBob) {
        row[dx] = bc;
      }
    }
  }
}

void stage_pulse(uint16_t* fb, const Snap& s, int ix, int iy, int iw,
                 int ah) {
  const int cx = ix + iw / 2;
  const int cy = iy + ah / 2;
  const int r_max = (iw < ah ? iw : ah) / 2 - 4;
  if (r_max < 16) {
    return;
  }
  const int r = 10 + static_cast<int>(
                         (static_cast<uint32_t>(r_max - 10) * s.in_beat) /
                         1000u);
  const bool attack = s.in_beat < 220;
  const int disc = s.beat == 1 ? 26 : 18;
  const int disc2 = disc * disc;
  const int outer_lo = (r_max - 3) * (r_max - 3);
  const int outer_hi = r_max * r_max;
  const int ring_lo = (r - 3) * (r - 3);
  const int ring_hi = r * r;
  const uint16_t disc_c = s.beat == 1 ? g_pal.hot : g_pal.neon;
  for (int py = -r_max; py <= r_max; ++py) {
    uint16_t* row = fb + (cy + py) * halesp::kLcdW + cx;
    for (int px = -r_max; px <= r_max; ++px) {
      const int d2 = px * px + py * py;
      if (d2 > outer_hi) {
        continue;
      }
      if (attack && d2 <= disc2) {
        row[px] = disc_c;
      } else if (d2 >= outer_lo) {
        row[px] = g_pal.border;
      } else if (d2 >= ring_lo && d2 <= ring_hi) {
        row[px] = g_pal.neon;
      }
    }
  }
}

void paint_stage(uint16_t* fb, const Snap& s) {
  const int x = g_lay.kStageX;
  const int y = g_lay.kStageY;
  const int w = g_lay.kStageW;
  const int h = g_lay.kStageH;
  const bool flash = s.playing && s.in_beat < 140;
  frame(fb, x, y, w, h, s.playing ? (flash ? g_pal.ink : g_pal.neon) : g_pal.border, 1);
  const int ix = x + 10;
  const int iy = y + 10;
  const int iw = w - 20;
  const uint32_t q = s.quantum != 0 ? s.quantum : 4;
  const int dots_y = y + h - 22;
  stage_dots(fb, x + w / 2, dots_y, q, s.beat, s.playing);
  const int ah = dots_y - 10 - iy;
  if (ah < 40 || iw < 40) {
    return;
  }
  if (!s.playing) {
    // Parked: a dim dash, no motion. The running stage is bright and
    // moving — the difference reads across the room.
    const int cell_h = ah / neon::ui::kHeroHeight;
    const int cell_w = iw / neon::ui::kHeroMaxWidth;
    int cell = cell_h < cell_w ? cell_h : cell_w;
    if (cell < 1) {
      cell = 1;
    }
    const int gw = hero_width("-", cell);
    hero_text(fb, ix + (iw - gw) / 2,
              iy + (ah - neon::ui::kHeroHeight * cell) / 2, "-", cell,
              g_pal.surface2);
    return;
  }
  if (s.big_beat == 0) {
    return;  // dots + flashing frame only
  }
  switch (static_cast<neon::BeatStyle>(s.beat_style)) {
    case neon::BeatStyle::kPie:
      stage_pie(fb, s, ix, iy, iw, ah);
      break;
    case neon::BeatStyle::kPendulum:
      stage_pendulum(fb, s, ix, iy, iw, ah);
      break;
    case neon::BeatStyle::kPulse:
      stage_pulse(fb, s, ix, iy, iw, ah);
      break;
    default:
      stage_number(fb, s, ix, iy, iw, ah);
      break;
  }
}

void paint_face(uint16_t* fb, const Snap& s, Touch pressed) {
  const int kW = g_lay.kW;
  const int kH = g_lay.kH;
  const int kPad = g_lay.kPad;
  fill(fb, 0, 0, kW, kH, g_pal.bg);

  paint_header(fb, s);

  const int gear_scale = g_lay.kGearS >= 56 ? 6 : 5;
  const int gear_px = 7 * gear_scale;
  const int gx = g_lay.kGearX + (g_lay.kGearS - gear_px) / 2;
  const int gy = g_lay.kGearY + (g_lay.kGearS - gear_px) / 2;
  if (pressed.hit == Hit::kGear) {
    fill(fb, g_lay.kGearX, g_lay.kGearY, g_lay.kGearS, g_lay.kGearS, g_pal.surface2);
    paint_gear(fb, gx, gy, gear_scale, g_pal.neon);
  } else {
    paint_gear(fb, gx, gy, gear_scale, g_pal.muted);
  }

  // BPM hero (tap-tempo zone). Seven-segment numerals, not scaled 5×7.
  const int bpm_w = hero_width(s.bpm, g_lay.kBpmCell);
  const int bpm_h = neon::ui::kHeroHeight * g_lay.kBpmCell;
  const int bpm_x = g_lay.kTapX + (g_lay.kTapW - bpm_w) / 2;
  const int bpm_y = g_lay.kTapY + (g_lay.kTapH - bpm_h) / 2;
  if (pressed.hit == Hit::kTap) {
    fill(fb, g_lay.kTapX, g_lay.kTapY, g_lay.kTapW, g_lay.kTapH, g_pal.surface);
  }
  hero_text(fb, bpm_x, bpm_y, s.bpm, g_lay.kBpmCell, g_pal.hero);

  paint_btn(fb, g_lay.kMinusX, g_lay.kMinusY, g_lay.kBtnW, g_lay.kBtnH, "-",
            g_lay.kBtnScale, pressed.hit == Hit::kMinus, false);
  paint_btn(fb, g_lay.kPlusX, g_lay.kPlusY, g_lay.kBtnW, g_lay.kBtnH, "+",
            g_lay.kBtnScale, pressed.hit == Hit::kPlus, false);

  const bool tr_press = pressed.hit == Hit::kTransport;
  const int tr_cx = g_lay.kTrX + g_lay.kTrW / 2;
  const int tr_ty = g_lay.kTrY + (g_lay.kTrH - 7 * g_lay.kTrScale) / 2;
  if (s.playing) {
    fill(fb, g_lay.kTrX, g_lay.kTrY, g_lay.kTrW, g_lay.kTrH,
         tr_press ? g_pal.ink : g_pal.hot);
    text_cx(fb, tr_cx, tr_ty, "STOP", g_lay.kTrScale, g_pal.bg);
  } else {
    fill(fb, g_lay.kTrX, g_lay.kTrY, g_lay.kTrW, g_lay.kTrH,
         tr_press ? g_pal.ink : g_pal.surface2);
    frame(fb, g_lay.kTrX, g_lay.kTrY, g_lay.kTrW, g_lay.kTrH, g_pal.border, 1);
    text_cx(fb, tr_cx, tr_ty, "RUN", g_lay.kTrScale, g_pal.ink);
  }

#if CONFIG_NEON_BOARD_LINKSYNC_TAB5
  paint_stage(fb, s);
#endif

  const int bar_y = g_lay.kBarY;
  const int bar_w = kW - 2 * kPad;
  const int bar_h = g_lay.kBarH;
  fill(fb, kPad, bar_y, bar_w, bar_h, g_pal.surface);
  const uint32_t span = s.quantum * 1000u;
  const uint32_t pos = span != 0 ? s.phase % span : 0;
  int fill_w =
      static_cast<int>((static_cast<uint64_t>(pos) * bar_w) / (span ? span : 1));
  if (fill_w < 0) {
    fill_w = 0;
  }
  if (fill_w > bar_w) {
    fill_w = bar_w;
  }
  if (s.playing) {
    fill(fb, kPad, bar_y, fill_w, bar_h, g_pal.neon_dim);
    const int head = 28;
    int hx = kPad + fill_w - head;
    if (hx < kPad) {
      hx = kPad;
    }
    fill(fb, hx, bar_y - 4, head, bar_h + 8, g_pal.neon);
  } else {
    fill(fb, kPad, bar_y, bar_w / 10, bar_h, g_pal.border);
  }
  for (uint32_t i = 1; i < s.quantum && i < 8; ++i) {
    const int tx = kPad + static_cast<int>((i * bar_w) / s.quantum);
    fill(fb, tx, bar_y - 4, 3, bar_h + 8, g_pal.border);
  }

  paint_footer(fb, s);
}

void wifi_ui_reset() {
  g_wifi = WifiPage::kOff;
  g_wifi_scroll = 0;
  g_kb_shift = false;
  g_kb_sym = false;
  g_kb_pass[0] = '\0';
  g_kb_ssid[0] = '\0';
  g_kb_open = false;
}

void wifi_scan_task(void*) {
  g_ap_n = neon_wifi_scan(g_aps, kMaxAps);
  g_scan_no_radio = !netman::wifi_driver_ready();
  g_wifi = WifiPage::kList;
  g_wifi_scroll = 0;
  vTaskDelete(nullptr);
}

void wifi_start_scan() {
  if (g_wifi == WifiPage::kScanning) {
    return;
  }
  g_wifi = WifiPage::kScanning;
  g_ap_n = 0;
  g_wifi_scroll = 0;
  g_scan_no_radio = false;
  g_scan_started_us = esp_timer_get_time();
  if (xTaskCreate(wifi_scan_task, "wifi_scan", 6144, nullptr, 4, nullptr) !=
      pdPASS) {
    g_scan_no_radio = !netman::wifi_driver_ready();
    g_wifi = WifiPage::kList;
  }
}

void wifi_join(const char* ssid, const char* pass) {
  neon::Config live = neon_config();
  std::memset(live.wifi[0].ssid, 0, sizeof(live.wifi[0].ssid));
  std::memset(live.wifi[0].pass, 0, sizeof(live.wifi[0].pass));
  std::snprintf(live.wifi[0].ssid, sizeof(live.wifi[0].ssid), "%s",
                ssid != nullptr ? ssid : "");
  std::snprintf(live.wifi[0].pass, sizeof(live.wifi[0].pass), "%s",
                pass != nullptr ? pass : "");
  neon_config_apply(live);
  g_cfg = live;
  neon_wifi_apply_credentials();
  g_wifi = WifiPage::kJoining;
  g_join_us = esp_timer_get_time();
}

int kb_key_h() { return g_lay.kH >= 800 ? 64 : 48; }

int kb_key_w() { return (g_lay.kW - 2 * g_lay.kPad) / 10; }

int kb_top() { return g_lay.kH - 4 * kb_key_h() - g_lay.kPad / 2; }

const char* kb_letters(int row) {
  if (g_kb_sym) {
    static const char* k[3] = {"1234567890", "-/:;()$&@\"", ".,?!'#+="};
    return (row >= 0 && row < 3) ? k[row] : "";
  }
  if (g_kb_shift) {
    static const char* k[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    return (row >= 0 && row < 3) ? k[row] : "";
  }
  static const char* k[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  return (row >= 0 && row < 3) ? k[row] : "";
}

void paint_wifi_overlay(uint16_t* fb, const Snap& s, Touch pressed) {
  const int kW = g_lay.kW;
  const int kH = g_lay.kH;
  const int kPad = g_lay.kPad;
  const int list_y = set_list_y();

  paint_btn(fb, kPad, 4, g_lay.kSetCloseW, g_lay.kSetHeadH - 8, "<", 3,
            pressed.hit == Hit::kWifiBack, false);

  if (g_wifi == WifiPage::kScanning) {
    text_cx(fb, kW / 2, list_y + 40, "SCANNING", 3, g_pal.ink);
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
    text_cx(fb, kW / 2, list_y + 80, "2.4 AND 5 GHZ", 2, g_pal.muted);
#else
    text_cx(fb, kW / 2, list_y + 80, "NEARBY 2.4 GHZ", 2, g_pal.muted);
#endif
    return;
  }

  if (g_wifi == WifiPage::kJoining) {
    text_cx(fb, kW / 2, list_y + 36, "CONNECTING", 3, g_pal.ink);
    text_cx(fb, kW / 2, list_y + 80, g_kb_ssid, 2, g_pal.muted);
    if (s.wifi_up) {
      text_cx(fb, kW / 2, list_y + 120, "JOINED", 3, g_pal.neon);
    } else if (esp_timer_get_time() - g_join_us > 12000000) {
      text_cx(fb, kW / 2, list_y + 120, "FAILED", 3, g_pal.danger);
      char why[48];
      std::snprintf(why, sizeof(why), "REASON %u",
                    static_cast<unsigned>(neon_wifi_last_disconnect_reason()));
      text_cx(fb, kW / 2, list_y + 156, why, 2, g_pal.muted);
    }
    return;
  }

  if (g_wifi == WifiPage::kList) {
    if (g_ap_n <= 0) {
      if (g_scan_no_radio) {
        text_cx(fb, kW / 2, list_y + 40, "NO RADIO", 3, g_pal.ink);
        text_cx(fb, kW / 2, list_y + 80, "TAP RST ON THE XIAO", 2, g_pal.muted);
        text_cx(fb, kW / 2, list_y + 110, "THEN SCAN AGAIN", 2, g_pal.muted);
      } else {
        text_cx(fb, kW / 2, list_y + 40, "NO NETWORKS", 3, g_pal.ink);
        text_cx(fb, kW / 2, list_y + 80, "TAP SCAN TO TRY AGAIN", 2,
                g_pal.muted);
      }
    }
    const int row_h = g_lay.kSetRowH;
    const int vis = (kH - list_y - row_h) / row_h;
    if (g_wifi_scroll < 0) {
      g_wifi_scroll = 0;
    }
    if (g_ap_n > vis && g_wifi_scroll > g_ap_n - vis) {
      g_wifi_scroll = g_ap_n - vis;
    }
    for (int i = 0; i < g_ap_n; ++i) {
      const int y = list_y + (i - g_wifi_scroll) * row_h;
      if (y + row_h <= list_y || y >= kH - row_h) {
        continue;
      }
      const bool hit = pressed.hit == Hit::kWifiAp && pressed.arg == i;
      fill(fb, 0, y, kW, row_h, hit ? g_pal.surface2 : g_pal.bg);
      fill(fb, kPad, y + row_h - 1, kW - 2 * kPad, 1, g_pal.surface);
      text(fb, kPad, y + (row_h - 14) / 2, g_aps[i].ssid, 2, g_pal.ink);
      char meta[16];
      std::snprintf(meta, sizeof(meta), "%s %s %d",
                    g_aps[i].channel > 14 ? "5G" : "2G",
                    g_aps[i].open ? "OPEN" : "LOCK",
                    static_cast<int>(g_aps[i].rssi));
      const int mw = text_width(meta, 2);
      text(fb, kW - kPad - mw, y + (row_h - 14) / 2, meta, 2, g_pal.muted);
    }
    paint_btn(fb, kPad, kH - row_h + 4, kW - 2 * kPad, row_h - 8, "SCAN AGAIN",
              2, pressed.hit == Hit::kWifiScan, false);
    return;
  }

  if (g_wifi != WifiPage::kKeyboard) {
    return;
  }

  text(fb, kPad, list_y + 8, g_kb_ssid, 2, g_pal.muted);
  char shown[72];
  if (g_kb_open) {
    std::snprintf(shown, sizeof(shown), "%s",
                  g_kb_pass[0] ? g_kb_pass : "OPEN NETWORK");
  } else if (g_kb_pass[0] == '\0') {
    std::snprintf(shown, sizeof(shown), "PASSWORD");
  } else {
    int n = static_cast<int>(std::strlen(g_kb_pass));
    int i = 0;
    for (; i < n && i < 60; ++i) {
      shown[i] = '*';
    }
    shown[i] = '\0';
  }
  text(fb, kPad, list_y + 32, shown, 3,
       g_kb_pass[0] ? g_pal.ink : g_pal.muted);
  fill(fb, kPad, list_y + 58, kW - 2 * kPad, 1, g_pal.border);

  const int kh = kb_key_h();
  const int kw = kb_key_w();
  const int top = kb_top();
  for (int row = 0; row < 3; ++row) {
    const char* letters = kb_letters(row);
    const int n = static_cast<int>(std::strlen(letters));
    const int x0 = (kW - n * kw) / 2;
    for (int col = 0; col < n; ++col) {
      char lab[2] = {letters[col], 0};
      const bool hit =
          pressed.hit == Hit::kKbKey && pressed.arg == row * 16 + col;
      paint_btn(fb, x0 + col * kw + 2, top + row * kh + 2, kw - 4, kh - 4, lab,
                2, hit, false);
    }
  }
  const int y3 = top + 3 * kh;
  const int x0 = kPad;
  paint_btn(fb, x0, y3 + 2, kw * 2 - 4, kh - 4, g_kb_shift ? "ABC" : "SHIFT", 2,
            pressed.hit == Hit::kKbShift, g_kb_shift);
  paint_btn(fb, x0 + kw * 2, y3 + 2, kw * 4 - 4, kh - 4, "SPACE", 2,
            pressed.hit == Hit::kKbKey && pressed.arg == 99, false);
  paint_btn(fb, x0 + kw * 6, y3 + 2, kw * 2 - 4, kh - 4, g_kb_sym ? "ABC" : "123",
            2, pressed.hit == Hit::kKbSym, g_kb_sym);
  paint_btn(fb, x0 + kw * 8, y3 + 2, kw * 2 - 4, kh - 4, "DEL", 2,
            pressed.hit == Hit::kKbBksp, false);
  paint_btn(fb, kW - kPad - kw * 3, list_y + 70, kw * 3, 40, "JOIN", 2,
            pressed.hit == Hit::kKbJoin, true);
}

void paint_settings(uint16_t* fb, const Snap& s, Touch pressed) {
  using S = neon::MenuModel::Screen;
  const int kW = g_lay.kW;
  const int kH = g_lay.kH;
  const int kPad = g_lay.kPad;
  fill(fb, 0, 0, kW, kH, g_pal.bg);

  const bool confirm = g_menu.screen() == S::kConfirm;
  const bool editing_clk = g_menu.screen() == S::kOutputEdit;

  fill(fb, 0, 0, kW, g_lay.kSetHeadH, g_pal.surface);
  fill(fb, 0, g_lay.kSetHeadH - 1, kW, 1, g_pal.border);
  if (editing_clk) {
    paint_btn(fb, kPad, 4, g_lay.kSetCloseW, g_lay.kSetHeadH - 8, "<", 3,
              pressed.hit == Hit::kBack, false);
    char title[16];
    std::snprintf(title, sizeof(title), "CLK %d", g_menu.output_index() + 1);
    text(fb, kPad + g_lay.kSetCloseW + 12, (g_lay.kSetHeadH - 21) / 2, title, 3,
         g_pal.ink);
  } else if (confirm) {
    text(fb, kPad, (g_lay.kSetHeadH - 21) / 2, "REBOOT", 3, g_pal.ink);
  } else {
    text(fb, kPad + (g_wifi != WifiPage::kOff ? g_lay.kSetCloseW + 8 : 0),
         (g_lay.kSetHeadH - 21) / 2,
         g_wifi != WifiPage::kOff ? "WIFI" : "SETTINGS", 3, g_pal.ink);
  }
  paint_btn(fb, kW - kPad - g_lay.kSetCloseW, 4, g_lay.kSetCloseW,
            g_lay.kSetHeadH - 8, "X", 3, pressed.hit == Hit::kClose, false);

  if (confirm) {
    text_cx(fb, kW / 2, g_lay.kSetHeadH + 40, "REBOOT THE MODULE?", 3, g_pal.ink);
    text_cx(fb, kW / 2, g_lay.kSetHeadH + 80, "CLOCK STOPS UNTIL IT RETURNS", 2,
            g_pal.muted);
    const int bw = (kW - 3 * kPad) / 2;
    const int by = kH / 2 + 20;
    const int bh = 88;
    paint_btn(fb, kPad, by, bw, bh, "NO", 4, pressed.hit == Hit::kConfirmNo,
              false);
    fill_cut(fb, kPad + bw + kPad, by, bw, bh,
             pressed.hit == Hit::kConfirmYes ? g_pal.ink : g_pal.hot);
    frame(fb, kPad + bw + kPad, by, bw, bh, g_pal.ink, 4);
    text_cx(fb, kPad + bw + kPad + bw / 2, by + (bh - 28) / 2, "YES", 4, g_pal.bg);
    return;
  }

  const int tab_w = kW / kTabCount;
  const int active = tab_index_for(g_menu.screen());
  for (int i = 0; i < kTabCount; ++i) {
    const int x = i * tab_w;
    const bool on = i == active;
    const bool hit = pressed.hit == Hit::kTab && pressed.arg == i;
    fill(fb, x, g_lay.kSetHeadH, tab_w, g_lay.kSetTabH,
         hit ? g_pal.surface2 : g_pal.bg);
    if (on) {
      fill(fb, x + 16, g_lay.kSetHeadH + g_lay.kSetTabH - 2, tab_w - 32, 2,
           g_pal.neon);
    }
    const int tw = text_width(kTabLabel[i], 2);
    text(fb, x + (tab_w - tw) / 2,
         g_lay.kSetHeadH + (g_lay.kSetTabH - 14) / 2, kTabLabel[i], 2,
         on ? g_pal.ink : g_pal.muted);
  }

  if (g_wifi != WifiPage::kOff) {
    paint_wifi_overlay(fb, s, pressed);
    return;
  }

  clamp_scroll(s);
  const int list_y = set_list_y();
  const int n = set_row_count(s);
  const int nudge_w = g_lay.kSetNudgeW;
  for (int i = 0; i < n; ++i) {
    const int y = list_y + i * g_lay.kSetRowH - g_scroll_px;
    if (y + g_lay.kSetRowH <= list_y || y >= kH) {
      continue;
    }
    char label[24] = {};
    char value[72] = {};
    if (g_menu.screen() == S::kNetwork) {
      net_row(s, i, label, sizeof(label), value, sizeof(value));
    } else {
      std::snprintf(label, sizeof(label), "%s", lcd_item_label(i));
      const int real = settings_real_index(i);
      if (g_menu.screen() == S::kSystem &&
          i == neon::MenuModel::kSystemVersionItem) {
        std::snprintf(value, sizeof(value), "%s", s.firmware);
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
      } else if (g_menu.screen() == S::kSystem && i == g_menu.item_count()) {
        std::snprintf(value, sizeof(value), "%s",
                      lcd_is_portrait() ? "PORT" : "LAND");
#endif
      } else {
        g_menu.item_value(real, value, sizeof(value));
      }
    }
    const bool row_press = pressed.hit == Hit::kRow && pressed.arg == i;
    fill(fb, 0, y, kW, g_lay.kSetRowH, row_press ? g_pal.surface2 : g_pal.bg);
    fill(fb, kPad, y + g_lay.kSetRowH - 1, kW - 2 * kPad, 1, g_pal.surface);
    const bool nudge = row_has_nudge(i);
    text(fb, kPad, y + (g_lay.kSetRowH - 14) / 2, label, 2, g_pal.muted);
    if (nudge) {
      const int plus_x = kW - kPad - nudge_w;
      const int minus_x = plus_x - 12 - nudge_w;
      paint_btn(fb, minus_x, y + 6, nudge_w, g_lay.kSetRowH - 12, "-", 3,
                pressed.hit == Hit::kRowMinus && pressed.arg == i, false);
      paint_btn(fb, plus_x, y + 6, nudge_w, g_lay.kSetRowH - 12, "+", 3,
                pressed.hit == Hit::kRowPlus && pressed.arg == i, false);
      const int vw = text_width(value, 2);
      text(fb, minus_x - 12 - vw, y + (g_lay.kSetRowH - 14) / 2, value, 2, g_pal.ink);
    } else {
      const int vw = text_width(value, 2);
      text(fb, kW - kPad - vw, y + (g_lay.kSetRowH - 14) / 2, value, 2, g_pal.ink);
    }
  }
}

Touch hit_at_live(int x, int y) {
  if (in_rect(x, y, g_lay.kGearX, g_lay.kGearY, g_lay.kGearS, g_lay.kGearS)) {
    return {Hit::kGear, 0};
  }
  if (in_rect(x, y, g_lay.kMinusX, g_lay.kMinusY, g_lay.kBtnW, g_lay.kBtnH)) {
    return {Hit::kMinus, 0};
  }
  if (in_rect(x, y, g_lay.kPlusX, g_lay.kPlusY, g_lay.kBtnW, g_lay.kBtnH)) {
    return {Hit::kPlus, 0};
  }
  if (in_rect(x, y, g_lay.kTrX, g_lay.kTrY, g_lay.kTrW, g_lay.kTrH)) {
    return {Hit::kTransport, 0};
  }
  if (in_rect(x, y, g_lay.kTapX, g_lay.kTapY, g_lay.kTapW, g_lay.kTapH)) {
    return {Hit::kTap, 0};
  }
  return {Hit::kNone, 0};
}

Touch hit_at_wifi(int x, int y) {
  const int kW = g_lay.kW;
  const int kH = g_lay.kH;
  const int kPad = g_lay.kPad;
  if (in_rect(x, y, kPad, 4, g_lay.kSetCloseW, g_lay.kSetHeadH - 8)) {
    return {Hit::kWifiBack, 0};
  }
  if (g_wifi == WifiPage::kList) {
    const int row_h = g_lay.kSetRowH;
    if (in_rect(x, y, kPad, kH - row_h + 4, kW - 2 * kPad, row_h - 8)) {
      return {Hit::kWifiScan, 0};
    }
    const int list_y = set_list_y();
    if (y >= list_y && y < kH - row_h) {
      const int i = (y - list_y) / row_h + g_wifi_scroll;
      if (i >= 0 && i < g_ap_n) {
        return {Hit::kWifiAp, i};
      }
    }
    return {Hit::kNone, 0};
  }
  if (g_wifi == WifiPage::kKeyboard) {
    const int kh = kb_key_h();
    const int kw = kb_key_w();
    const int top = kb_top();
    const int list_y = set_list_y();
    if (in_rect(x, y, kW - kPad - kw * 3, list_y + 70, kw * 3, 40)) {
      return {Hit::kKbJoin, 0};
    }
    if (y >= top + 3 * kh) {
      const int x0 = kPad;
      if (in_rect(x, y, x0, top + 3 * kh + 2, kw * 2 - 4, kh - 4)) {
        return {Hit::kKbShift, 0};
      }
      if (in_rect(x, y, x0 + kw * 2, top + 3 * kh + 2, kw * 4 - 4, kh - 4)) {
        return {Hit::kKbKey, 99};  // space
      }
      if (in_rect(x, y, x0 + kw * 6, top + 3 * kh + 2, kw * 2 - 4, kh - 4)) {
        return {Hit::kKbSym, 0};
      }
      if (in_rect(x, y, x0 + kw * 8, top + 3 * kh + 2, kw * 2 - 4, kh - 4)) {
        return {Hit::kKbBksp, 0};
      }
      return {Hit::kNone, 0};
    }
    if (y >= top) {
      const int row = (y - top) / kh;
      if (row >= 0 && row < 3) {
        const char* letters = kb_letters(row);
        const int n = static_cast<int>(std::strlen(letters));
        const int x0 = (kW - n * kw) / 2;
        const int col = (x - x0) / kw;
        if (col >= 0 && col < n) {
          return {Hit::kKbKey, row * 16 + col};
        }
      }
    }
    return {Hit::kNone, 0};
  }
  return {Hit::kNone, 0};
}

Touch hit_at_settings(int x, int y, const Snap& s) {
  using S = neon::MenuModel::Screen;
  const int kW = g_lay.kW;
  const int kPad = g_lay.kPad;
  if (in_rect(x, y, kW - kPad - g_lay.kSetCloseW, 4, g_lay.kSetCloseW,
              g_lay.kSetHeadH - 8)) {
    return {Hit::kClose, 0};
  }
  if (g_wifi != WifiPage::kOff) {
    if (y >= g_lay.kSetHeadH && y < set_list_y()) {
      const int tab_w = kW / kTabCount;
      int t = x / tab_w;
      if (t < 0) t = 0;
      if (t >= kTabCount) t = kTabCount - 1;
      return {Hit::kTab, t};
    }
    return hit_at_wifi(x, y);
  }
  if (g_menu.screen() == S::kOutputEdit &&
      in_rect(x, y, kPad, 4, g_lay.kSetCloseW, g_lay.kSetHeadH - 8)) {
    return {Hit::kBack, 0};
  }
  if (g_menu.screen() == S::kConfirm) {
    const int bw = (kW - 3 * kPad) / 2;
    const int by = g_lay.kH / 2 + 20;
    const int bh = 88;
    if (in_rect(x, y, kPad, by, bw, bh)) {
      return {Hit::kConfirmNo, 0};
    }
    if (in_rect(x, y, kPad + bw + kPad, by, bw, bh)) {
      return {Hit::kConfirmYes, 0};
    }
    return {Hit::kNone, 0};
  }
  if (y >= g_lay.kSetHeadH && y < set_list_y()) {
    const int tab_w = kW / kTabCount;
    int t = x / tab_w;
    if (t < 0) {
      t = 0;
    }
    if (t >= kTabCount) {
      t = kTabCount - 1;
    }
    return {Hit::kTab, t};
  }
  const int list_y = set_list_y();
  if (y < list_y) {
    return {Hit::kNone, 0};
  }
  const int i = (y - list_y + g_scroll_px) / g_lay.kSetRowH;
  if (i < 0 || i >= set_row_count(s)) {
    return {Hit::kNone, 0};
  }
  if (row_has_nudge(i)) {
    const int plus_x = kW - kPad - g_lay.kSetNudgeW;
    const int minus_x = plus_x - 12 - g_lay.kSetNudgeW;
    const int ry = list_y + i * g_lay.kSetRowH - g_scroll_px;
    if (in_rect(x, y, plus_x, ry + 6, g_lay.kSetNudgeW, g_lay.kSetRowH - 12)) {
      return {Hit::kRowPlus, i};
    }
    if (in_rect(x, y, minus_x, ry + 6, g_lay.kSetNudgeW, g_lay.kSetRowH - 12)) {
      return {Hit::kRowMinus, i};
    }
  }
  return {Hit::kRow, i};
}

Touch hit_at(int x, int y, const Snap& s) {
  return g_settings ? hit_at_settings(x, y, s) : hit_at_live(x, y);
}

void handle_row_tap(int i) {
  using S = neon::MenuModel::Screen;
  const S scr = g_menu.screen();
  if (scr == S::kOutputs) {
    g_menu.set_output_index(i);
    g_menu.go_section(S::kOutputEdit);
    g_scroll_px = 0;
    return;
  }
  if (scr == S::kNetwork) {
    if (i == 0) {
      cycle_ap_policy(1);
      return;
    }
    Snap s{};
    s.setup_ap = netman::ap_is_up();
    if (i == network_scan_row(s)) {
      wifi_start_scan();
    }
    return;
  }
  if (scr == S::kSystem && i == neon::MenuModel::kSystemRebootItem) {
    g_menu.go_section(S::kConfirm);
    return;
  }
  if (scr == S::kSystem && i == neon::MenuModel::kSystemVersionItem) {
    return;
  }
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
  if (scr == S::kSystem && i == g_menu.item_count()) {
    g_cfg.display_portrait = lcd_is_portrait() ? 2 : 1;
    neon::Config live = neon_config();
    live.display_portrait = g_cfg.display_portrait;
    neon_config_apply(live);
    rebuild_layout();
    return;
  }
#endif
  const int real = settings_real_index(i);
  g_menu.set_cursor(real);
  char val[24] = {};
  g_menu.item_value(real, val, sizeof(val));
  if (std::strcmp(val, "ON") == 0 || std::strcmp(val, "LEAD") == 0) {
    g_menu.nudge_value(-1);
  } else {
    g_menu.nudge_value(1);
  }
}

void fire_live(Touch t) {
  if (t.hit == Hit::kGear) {
    open_settings();
    ESP_LOGI(kTag, "touch settings");
    return;
  }
  ControlCommand cmd{};
  switch (t.hit) {
    case Hit::kMinus:
      cmd.kind = ControlCommand::Kind::kNudgeTempo;
      cmd.arg = -1;
      break;
    case Hit::kPlus:
      cmd.kind = ControlCommand::Kind::kNudgeTempo;
      cmd.arg = 1;
      break;
    case Hit::kTransport:
      cmd.kind = ControlCommand::Kind::kToggle;
      break;
    case Hit::kTap:
      cmd.kind = ControlCommand::Kind::kTapTempo;
      break;
    default:
      return;
  }
  if (!control_queue_push(cmd)) {
    ESP_LOGW(kTag, "control queue full");
    return;
  }
  ESP_LOGI(kTag, "touch %s",
           t.hit == Hit::kMinus       ? "-1 BPM"
           : t.hit == Hit::kPlus      ? "+1 BPM"
           : t.hit == Hit::kTransport ? "toggle"
                                      : "tap tempo");
}

void kb_push(char ch) {
  const int n = static_cast<int>(std::strlen(g_kb_pass));
  if (n + 1 >= static_cast<int>(sizeof(g_kb_pass))) {
    return;
  }
  g_kb_pass[n] = ch;
  g_kb_pass[n + 1] = '\0';
}

void fire_wifi(Touch t) {
  switch (t.hit) {
    case Hit::kWifiBack:
      if (g_wifi == WifiPage::kKeyboard) {
        g_wifi = WifiPage::kList;
        g_kb_pass[0] = '\0';
      } else {
        wifi_ui_reset();
      }
      return;
    case Hit::kWifiScan:
      wifi_start_scan();
      return;
    case Hit::kWifiAp:
      if (t.arg >= 0 && t.arg < g_ap_n) {
        std::snprintf(g_kb_ssid, sizeof(g_kb_ssid), "%s", g_aps[t.arg].ssid);
        g_kb_open = g_aps[t.arg].open != 0;
        g_kb_pass[0] = '\0';
        g_kb_shift = false;
        g_kb_sym = false;
        if (g_kb_open) {
          wifi_join(g_kb_ssid, "");
        } else {
          g_wifi = WifiPage::kKeyboard;
        }
      }
      return;
    case Hit::kKbShift:
      g_kb_shift = !g_kb_shift;
      g_kb_sym = false;
      return;
    case Hit::kKbSym:
      g_kb_sym = !g_kb_sym;
      g_kb_shift = false;
      return;
    case Hit::kKbBksp: {
      const int n = static_cast<int>(std::strlen(g_kb_pass));
      if (n > 0) {
        g_kb_pass[n - 1] = '\0';
      }
      return;
    }
    case Hit::kKbJoin:
      wifi_join(g_kb_ssid, g_kb_pass);
      return;
    case Hit::kKbKey:
      if (t.arg == 99) {
        kb_push(' ');
        return;
      }
      {
        const int row = t.arg / 16;
        const int col = t.arg % 16;
        const char* letters = kb_letters(row);
        const int n = static_cast<int>(std::strlen(letters));
        if (row >= 0 && row < 3 && col >= 0 && col < n) {
          kb_push(letters[col]);
        }
      }
      return;
    default:
      return;
  }
}

void fire_settings(Touch t) {
  using S = neon::MenuModel::Screen;
  switch (t.hit) {
    case Hit::kClose:
      close_settings();
      ESP_LOGI(kTag, "settings close");
      return;
    case Hit::kWifiBack:
    case Hit::kWifiScan:
    case Hit::kWifiAp:
    case Hit::kKbKey:
    case Hit::kKbShift:
    case Hit::kKbSym:
    case Hit::kKbBksp:
    case Hit::kKbJoin:
      fire_wifi(t);
      return;
    case Hit::kBack:
      if (g_menu.screen() == S::kOutputEdit) {
        const int out = g_menu.output_index();
        g_menu.go_section(S::kOutputs);
        g_menu.set_cursor(out);
        g_scroll_px = 0;
      }
      break;
    case Hit::kTab:
      if (t.arg >= 0 && t.arg < kTabCount) {
        wifi_ui_reset();
        g_menu.go_section(kTabs[t.arg]);
        g_scroll_px = 0;
      }
      break;
    case Hit::kRow:
      handle_row_tap(t.arg);
      break;
    case Hit::kRowMinus:
      if (g_menu.screen() == S::kNetwork && t.arg == 0) {
        cycle_ap_policy(-1);
        break;
      }
      g_menu.set_cursor(settings_real_index(t.arg));
      g_menu.nudge_value(-1);
      break;
    case Hit::kRowPlus:
      if (g_menu.screen() == S::kNetwork && t.arg == 0) {
        cycle_ap_policy(1);
        break;
      }
      g_menu.set_cursor(settings_real_index(t.arg));
      g_menu.nudge_value(1);
      break;
    case Hit::kConfirmYes:
      g_menu.set_confirm_yes(true);
      g_menu.on_click();
      break;
    case Hit::kConfirmNo:
      g_menu.set_confirm_yes(false);
      g_menu.on_click();
      break;
    default:
      break;
  }
  if (g_menu.take_action() == neon::MenuModel::Action::kReboot) {
    ESP_LOGI(kTag, "reboot from settings");
    neon_config_flush_now();
    esp_restart();
  }
  commit_menu();
}

void fire(Touch t) {
  if (t.hit == Hit::kNone) {
    return;
  }
  if (g_settings) {
    fire_settings(t);
  } else {
    fire_live(t);
  }
}

Touch poll_touch(const Snap& s) {
  int x = 0;
  int y = 0;
  const bool down = halesp::lcd_touch_poll(&x, &y);
  if (down) {
    int lx = x;
    int ly = y;
    from_phys(x, y, &lx, &ly);
    x = lx;
    y = ly;
  }
  const int64_t now = esp_timer_get_time();
  if (down) {
    g_dimmer.note_activity(now);
  }
  if (g_swallow_touch) {
    // This gesture only woke the blanked panel; drop it whole.
    if (!down) {
      g_swallow_touch = false;
    }
    return Touch{};
  }
  static bool was_down = false;
  static bool press_in_settings = false;
  static Touch held{};
  static int down_x = 0;
  static int down_y = 0;
  static int last_y = 0;
  static bool dragging = false;
  static int64_t down_us = 0;
  static int64_t last_fire_us = 0;
  static int log_left = 8;

  if (!down) {
    Touch released{};
    // Only the lift of a gesture that *started* on the settings panel
    // may fire a settings hit. The gear is top-right on the live face;
    // Close is top-right on settings — treating that same lift as Close
    // made the panel a momentary button.
    if (was_down && press_in_settings && !dragging) {
      const bool already =
          held.hit == Hit::kRowMinus || held.hit == Hit::kRowPlus ||
          held.hit == Hit::kKbKey || held.hit == Hit::kKbBksp ||
          held.hit == Hit::kKbShift || held.hit == Hit::kKbSym ||
          held.hit == Hit::kKbJoin;
      if (!already) {
        released = hit_at(down_x, down_y, s);
        fire(released);
      }
    }
    was_down = false;
    press_in_settings = false;
    held = {};
    dragging = false;
    return released;
  }

  const Touch hit = hit_at(x, y, s);
  if (log_left > 0) {
    ESP_LOGI(kTag, "touch xy=%d,%d hit=%u arg=%d", x, y,
             static_cast<unsigned>(hit.hit), hit.arg);
    --log_left;
  }
  if (!was_down) {
    was_down = true;
    press_in_settings = g_settings;
    held = hit;
    down_x = x;
    down_y = y;
    last_y = y;
    dragging = false;
    down_us = now;
    last_fire_us = now;
    const bool press_now =
        !g_settings || hit.hit == Hit::kRowMinus || hit.hit == Hit::kRowPlus ||
        hit.hit == Hit::kKbKey || hit.hit == Hit::kKbBksp ||
        hit.hit == Hit::kKbShift || hit.hit == Hit::kKbSym ||
        hit.hit == Hit::kKbJoin;
    if (press_now) {
      fire(hit);
    }
    return hit;
  }

  if (g_settings && !press_in_settings) {
    // Finger still down after opening via the gear. Swallow it.
    return {};
  }

  if (g_settings) {
    if (std::abs(y - down_y) > 20) {
      dragging = true;
    }
    if (dragging) {
      if (g_wifi == WifiPage::kList) {
        if (std::abs(y - last_y) >= g_lay.kSetRowH / 2) {
          g_wifi_scroll += (last_y > y) ? 1 : -1;
          if (g_wifi_scroll < 0) {
            g_wifi_scroll = 0;
          }
          last_y = y;
        }
      } else if (g_wifi == WifiPage::kOff) {
        g_scroll_px += last_y - y;
        last_y = y;
        clamp_scroll(s);
      }
      return {};
    }
  }

  const bool hold =
      (hit.hit == Hit::kMinus || hit.hit == Hit::kPlus ||
       hit.hit == Hit::kRowMinus || hit.hit == Hit::kRowPlus ||
       hit.hit == Hit::kKbBksp) &&
      hit.hit == held.hit && hit.arg == held.arg;
  if (hold && now - down_us > 400000 && now - last_fire_us > 120000) {
    last_fire_us = now;
    fire(hit);
  }
  return hit;
}

void paint(uint16_t* fb, const Snap& s, Touch pressed) {
  rebuild_layout();
  refresh_pal(g_settings ? g_cfg.color_theme : neon_config().color_theme);
  if (g_wifi == WifiPage::kScanning) {
    const int64_t dt = esp_timer_get_time() - g_scan_started_us;
    if (!netman::wifi_driver_ready() && dt > 1500000) {
      g_scan_no_radio = true;
      g_wifi = WifiPage::kList;
    } else if (dt > 20000000) {
      g_scan_no_radio = !netman::wifi_driver_ready();
      g_wifi = WifiPage::kList;
    }
  }
  if (g_wifi == WifiPage::kJoining && s.wifi_up &&
      esp_timer_get_time() - g_join_us > 1200000) {
    wifi_ui_reset();
  }
  if (g_settings) {
    paint_settings(fb, s, pressed);
  } else {
    paint_face(fb, s, pressed);
  }
}

void lcd_task(void*) {
  rebuild_layout();
  if (!halesp::lcd_rgb_init()) {
    ESP_LOGE(kTag, "LCD init failed");
    vTaskDelete(nullptr);
    return;
  }
  if (!halesp::lcd_touch_init()) {
    ESP_LOGW(kTag, "touch init failed — display only");
  }
  g_cfg = neon_config();
  g_dimmer.note_activity(esp_timer_get_time());
  apply_effective_backlight(esp_timer_get_time());
  // Compose straight into the panel's back buffer where the HAL offers
  // one (RGB path): no full-frame copy racing the scanout, and the flip
  // lands on a frame boundary so large repaints (RUN↔STOP) cannot tear.
  const bool dbuf = halesp::lcd_rgb_next_frame() != nullptr;
  uint16_t* own_fb = nullptr;
  if (!dbuf) {
    own_fb = static_cast<uint16_t*>(heap_caps_malloc(
        static_cast<size_t>(halesp::kLcdW) * halesp::kLcdH * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (own_fb == nullptr) {
      ESP_LOGE(kTag, "no PSRAM for %dx%d framebuffer", halesp::kLcdW,
               halesp::kLcdH);
      vTaskDelete(nullptr);
      return;
    }
  }
  ESP_LOGI(kTag, "live panel %dx%d touch=%d %s", halesp::kLcdW, halesp::kLcdH,
           halesp::lcd_touch_ok() ? 1 : 0,
           dbuf ? "double-buffered" : "blit");
  for (;;) {
    const int64_t now = esp_timer_get_time();
    {
      const neon::Config& live = neon_config();
      g_dimmer.configure(live.display_dim_s, live.display_dim_level);
    }
    if (g_dimmer.level(now) == neon::ui::IdleDimmer::Level::kBlank) {
      // Panel dark: drain the touch without acting on it (the waking
      // tap must not press a control blind) and skip composition — on
      // the Tab5 that is a 1.8 MB blit saved every frame.
      int tx = 0;
      int ty = 0;
      if (halesp::lcd_touch_poll(&tx, &ty)) {
        g_dimmer.note_activity(now);
        g_swallow_touch = true;
      }
      // A transport started remotely must un-blank (playing never
      // blanks — the tempo stays glanceable).
      neon::TimelineSnapshot tl{};
      timeline_bus().read(tl);
      g_dimmer.set_playing(tl.playing != 0);
      apply_effective_backlight(now);
      vTaskDelay(pdMS_TO_TICKS(g_dimmer.frame_interval_hint_ms(now)));
      continue;
    }
    const Snap s = snapshot();
    const Touch pressed = poll_touch(s);
    g_dimmer.set_playing(s.playing);
    apply_effective_backlight(now);
    const int hint = g_dimmer.frame_interval_hint_ms(now);
    if (dbuf) {
      uint16_t* fb = halesp::lcd_rgb_next_frame();
      paint(fb, s, pressed);
      if (!halesp::lcd_rgb_present()) {
        ESP_LOGW(kTag, "present failed");
      }
      // present() already blocked to the frame boundary (~60 Hz); one
      // tick keeps touch responsive and lands the loop near 30 fps.
      vTaskDelay(pdMS_TO_TICKS(15 + hint));
    } else {
      paint(own_fb, s, pressed);
      if (!halesp::lcd_rgb_blit(own_fb, 0, 0, halesp::kLcdW, halesp::kLcdH)) {
        ESP_LOGW(kTag, "blit failed");
      }
      vTaskDelay(pdMS_TO_TICKS(40 + hint));  // ~25 Hz active, relaxed dim
    }
  }
}

}  // namespace

void neon_start_lcd_service() {
  xTaskCreatePinnedToCore(lcd_task, "lcd", 12288, nullptr, 3, nullptr, 0);
}

#else

void neon_start_lcd_service() {}

#endif
