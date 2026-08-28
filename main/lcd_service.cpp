#include "sdkconfig.h"
#include "tasks.h"

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

// Tab5 neon tubes on a near-black void — both LCD flavors share this.
constexpr uint16_t kBg = rgb(0, 2, 8);
constexpr uint16_t kSurface = rgb(0, 28, 40);
constexpr uint16_t kSurface2 = rgb(0, 48, 64);
constexpr uint16_t kBorder = rgb(0, 140, 160);
constexpr uint16_t kInk = rgb(255, 255, 255);
constexpr uint16_t kMuted = rgb(0, 200, 210);
constexpr uint16_t kNeon = rgb(0, 255, 255);
constexpr uint16_t kNeonDim = rgb(0, 180, 200);
constexpr uint16_t kHot = rgb(255, 45, 149);  // STOP tube; cyan stays the live accent

void fill(uint16_t* fb, int x, int y, int w, int h, uint16_t c) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > halesp::kLcdW) {
    w = halesp::kLcdW - x;
  }
  if (y + h > halesp::kLcdH) {
    h = halesp::kLcdH - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }
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
  std::snprintf(s.bpm, sizeof(s.bpm), "%u.%u",
                static_cast<unsigned>(s.milli_bpm / 1000u),
                static_cast<unsigned>((s.milli_bpm / 100u) % 10u));
  netman::primary_ip(s.ip, sizeof(s.ip));
  const char* ssid = neon_wifi_current_ssid();
  std::snprintf(s.wifi_ssid, sizeof(s.wifi_ssid), "%s",
                ssid != nullptr ? ssid : "");
  const esp_app_desc_t* desc = esp_app_get_description();
  std::snprintf(s.firmware, sizeof(s.firmware), "%s",
                desc != nullptr ? desc->version : "unknown");
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
};

struct Touch {
  Hit hit = Hit::kNone;
  int arg = 0;
};

#if CONFIG_NEON_BOARD_LINKSYNC_TAB5
// 720×1280 portrait — fat hit targets, hero BPM, beat stage, phase strip.
namespace lay {
constexpr int kW = halesp::kLcdW;
constexpr int kH = halesp::kLcdH;
constexpr int kPad = 32;
constexpr int kHeadH = 64;
constexpr int kTapX = kPad;
constexpr int kTapY = 80;
constexpr int kTapW = kW - 2 * kPad;
constexpr int kTapH = 232;
constexpr int kBtnW = 280;
constexpr int kBtnH = 150;
constexpr int kMinusX = kPad;
constexpr int kMinusY = 332;
constexpr int kPlusX = kW - kPad - kBtnW;
constexpr int kPlusY = kMinusY;
constexpr int kTrX = kPad;
constexpr int kTrY = 502;
constexpr int kTrW = kW - 2 * kPad;
constexpr int kTrH = 170;
constexpr int kStageX = kPad;
constexpr int kStageY = 692;
constexpr int kStageW = kW - 2 * kPad;
constexpr int kStageH = 440;
constexpr int kBarY = 1148;
constexpr int kBarH = 18;
constexpr int kFootY = 1180;
constexpr int kFootLine1 = 1188;
constexpr int kFootLine2 = 1224;
constexpr int kFootScale = 3;
constexpr int kBpmCell = 6;
constexpr int kTapLabelScale = 3;
constexpr int kTrScale = 8;
constexpr int kBtnScale = 8;
constexpr int kGearS = 56;
constexpr int kGearX = kW - kPad - kGearS;
constexpr int kGearY = 4;
constexpr int kSetHeadH = 64;
constexpr int kSetTabH = 64;
constexpr int kSetRowH = 80;
constexpr int kSetCloseW = 80;
constexpr int kSetNudgeW = 80;
}  // namespace lay
#else
// CrowPanel 800×480 landscape. Controls in a left column, the beat stage
// on the right, one status band at the bottom — nothing shares pixels.
namespace lay {
constexpr int kW = halesp::kLcdW;
constexpr int kH = halesp::kLcdH;
constexpr int kPad = 24;
constexpr int kHeadH = 48;
constexpr int kTapX = kPad;
constexpr int kTapY = 60;
constexpr int kTapW = 484;
constexpr int kTapH = 126;
constexpr int kBtnW = 190;
constexpr int kBtnH = 90;
constexpr int kMinusX = kPad;
constexpr int kMinusY = 204;
constexpr int kPlusX = kTapX + kTapW - kBtnW;
constexpr int kPlusY = kMinusY;
constexpr int kTrX = kPad;
constexpr int kTrY = 312;
constexpr int kTrW = kTapW;
constexpr int kTrH = 90;
constexpr int kStageX = 528;
constexpr int kStageY = 60;
constexpr int kStageW = kW - kPad - kStageX;
constexpr int kStageH = 342;
constexpr int kBarY = 412;
constexpr int kBarH = 12;
constexpr int kFootY = 432;
constexpr int kFootLine1 = 436;
constexpr int kFootLine2 = 458;
constexpr int kFootScale = 2;
constexpr int kBpmCell = 4;
constexpr int kTapLabelScale = 2;
constexpr int kTrScale = 6;
constexpr int kBtnScale = 6;
constexpr int kGearS = 40;
constexpr int kGearX = kW - kPad - kGearS;
constexpr int kGearY = 4;
constexpr int kSetHeadH = 44;
constexpr int kSetTabH = 48;
constexpr int kSetRowH = 52;
constexpr int kSetCloseW = 56;
constexpr int kSetNudgeW = 56;
}  // namespace lay
#endif

bool in_rect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

neon::Config g_cfg;
neon::MenuModel g_menu(&g_cfg);
bool g_settings = false;
int g_scroll_px = 0;

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

int set_list_y() { return lay::kSetHeadH + lay::kSetTabH; }

int set_list_h() { return lay::kH - set_list_y(); }

int set_row_count(const Snap& s) {
  using S = neon::MenuModel::Screen;
  const S scr = g_menu.screen();
  if (scr == S::kNetwork) {
    return s.setup_ap ? 6 : 5;
  }
  if (scr == S::kConfirm) {
    return 0;
  }
  return g_menu.item_count();
}

int max_scroll(const Snap& s) {
  const int content = set_row_count(s) * lay::kSetRowH;
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
  halesp::lcd_rgb_backlight(static_cast<uint8_t>((brightness * 100u) / 255u));
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
  apply_backlight(live.display_brightness);
}

void open_settings() {
  g_cfg = neon_config();
  g_menu.go_section(neon::MenuModel::Screen::kMidi);
  g_scroll_px = 0;
  g_settings = true;
}

void close_settings() {
  commit_menu();
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
        "VERSION",    "REBOOT"};
    if (i >= 0 && i < 15) {
      return k[i];
    }
  } else if (scr == S::kAudio) {
    static const char* k[] = {"AUDIO",  "METRONOME", "CLICK",   "SOUND",
                              "OUT L",  "OUT R",     "LINE IN", "PUBLISH",
                              "SUB"};
    if (i >= 0 && i < 9) {
      return k[i];
    }
  }
  return g_menu.item_label(i);
}

bool row_has_nudge(int i) {
  using S = neon::MenuModel::Screen;
  const S scr = g_menu.screen();
  if (scr == S::kOutputs || scr == S::kNetwork || scr == S::kConfirm) {
    return false;
  }
  if (scr == S::kSystem && (i == neon::MenuModel::kSystemVersionItem ||
                            i == neon::MenuModel::kSystemRebootItem)) {
    return false;
  }
  return true;
}

void net_row(const Snap& s, int i, char* label, int lcap, char* value,
             int vcap) {
  const neon::Config& cfg = neon_config();
  label[0] = '\0';
  value[0] = '\0';
  switch (i) {
    case 0:
      std::snprintf(label, lcap, "MODE");
      std::snprintf(value, vcap, "%s",
                    s.setup_ap ? "SETUP AP" : (s.wifi_up ? "STA" : "OFF"));
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
        std::snprintf(label, lcap, "WIFI EDIT");
        std::snprintf(value, vcap, "USE WEB UI");
      }
      break;
    default:
      std::snprintf(label, lcap, "WIFI EDIT");
      std::snprintf(value, vcap, "USE WEB UI");
      break;
  }
}

void paint_btn(uint16_t* fb, int x, int y, int w, int h, const char* label,
               int scale, bool pressed, bool accent) {
  const uint16_t fill_c =
      pressed ? kNeon : (accent ? kSurface2 : kSurface);
  fill_cut(fb, x, y, w, h, fill_c);
  frame(fb, x, y, w, h, pressed ? kInk : kNeon, pressed ? 5 : 3);
  text_cx(fb, x + w / 2, y + (h - 7 * scale) / 2, label, scale,
          pressed ? kBg : kInk);
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
  constexpr int kW = lay::kW;
  constexpr int kPad = lay::kPad;
  // Fixed 4 px rail: the beat stage carries the motion now, so the header
  // stops pulsing (the growing rail used to push the name into the BPM
  // plate on beat 1).
  fill(fb, 0, 0, kW, 4, s.playing ? kNeon : kNeonDim);
  const int ty = (lay::kHeadH - 14) / 2 + 4;
  char peers[24];
  std::snprintf(peers, sizeof(peers), "%u", static_cast<unsigned>(s.peers));
  const int peers_w = text_width(peers, 3);
  const int right = lay::kGearX - 16;
  text(fb, right - peers_w, ty - 4, peers, 3, kNeon);
  const int label_x = right - peers_w - text_width("PEERS", 2) - 10;
  text(fb, label_x, ty, "PEERS", 2, kMuted);
  text_clip(fb, kPad, ty, s.name, 2, kInk, label_x - kPad - 12);
}

void paint_footer(uint16_t* fb, const Snap& s) {
  constexpr int kW = lay::kW;
  constexpr int kPad = lay::kPad;
  const int sc = lay::kFootScale;
  const int y1 = lay::kFootLine1;
  const int y2 = lay::kFootLine2;
  const neon::Config& cfg = neon_config();
  const int half = kW / 2;
  if (s.show_ap) {
    const char* url = "192.168.4.1";
    const int url_w = text_width(url, sc);
    text(fb, kPad, y1, "SETUP AP", sc, kNeon);
    text_clip(fb, kPad + text_width("SETUP AP  ", sc), y1,
              s.ap_ssid[0] ? s.ap_ssid : "LINK-LCD", sc, kInk,
              kW - 2 * kPad - text_width("SETUP AP  ", sc) - url_w - 16);
    text(fb, kW - kPad - url_w, y1, url, sc, kMuted);
    text(fb, kPad, y2, "PASS", sc, kMuted);
    text_clip(fb, kPad + text_width("PASS  ", sc), y2, cfg.ap_pass, sc, kInk,
              kW - 2 * kPad - text_width("PASS  ", sc));
  } else if (!s.provisioned) {
    text(fb, kPad, y1, "NO WIFI", sc, kNeon);
    text(fb, kPad, y2, "SoftAP did not start", sc, kMuted);
  } else {
    const char* net = s.wifi_up ? (s.wifi_ssid[0] ? s.wifi_ssid : "-")
                                : "CONNECTING";
    const char* midi = "MIDI CLOCK 24 PPQN";
    const int midi_w = text_width(midi, sc);
    text(fb, kPad, y1, s.wifi_up ? "STA" : "NET", sc, kNeon);
    text_clip(fb, kPad + text_width("STA  ", sc), y1, net, sc, kInk,
              kW - kPad - midi_w - 16 - (kPad + text_width("STA  ", sc)));
    text(fb, kW - kPad - midi_w, y1, midi, sc, kMuted);
    text(fb, kPad, y2, "IP", sc, kMuted);
    text(fb, kPad + text_width("IP  ", sc), y2, s.ip[0] ? s.ip : "-", sc,
         kInk);
    text_clip(fb, half + 24, y2, s.firmware, sc, kMuted, kW - kPad - half - 24);
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
    uint16_t c = kSurface2;
    if (playing && i == beat) {
      c = beat == 1 ? kHot : kNeon;
    } else if (playing && i < beat) {
      c = kNeonDim;
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
    fill(fb, ix, iy, iw, ah, kSurface);
  }
  const int x = ix + (iw - ink_w * cell) / 2 - ink_lo * cell;
  const int y = iy + (ah - neon::ui::kHeroHeight * cell) / 2;
  hero_text(fb, x, y, d, cell, s.beat == 1 ? kHot : kNeon);
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
        row[px] = kNeonDim;
        continue;
      }
      const int ce = px * ey - py * ex;
      const bool in = wide ? !((ex * py - ey * px) >= 0 && px <= 0)
                           : (px >= 0 && ce >= 0);
      if (in) {
        row[px] = kNeon;
      }
    }
  }
  fill(fb, cx - 4, cy - 4, 8, 8, kInk);
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
  fill(fb, px0 - tx - 2, ty + 20, 4, 14, kBorder);
  fill(fb, px0 + tx - 2, ty + 20, 4, 14, kBorder);
  constexpr int kSteps = 26;
  for (int i = 0; i <= kSteps; ++i) {
    const int xi = px0 + ((bx - px0) * i) / kSteps;
    const int yi = py0 + ((by - py0) * i) / kSteps;
    fill(fb, xi - 3, yi - 3, 6, 6, kNeonDim);
  }
  fill(fb, px0 - 5, py0 - 5, 10, 10, kInk);
  constexpr int kBob = 16;
  const uint16_t bc = (s.beat == 1 && s.in_beat < 140) ? kHot : kNeon;
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
  const uint16_t disc_c = s.beat == 1 ? kHot : kNeon;
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
        row[px] = kBorder;
      } else if (d2 >= ring_lo && d2 <= ring_hi) {
        row[px] = kNeon;
      }
    }
  }
}

void paint_stage(uint16_t* fb, const Snap& s) {
  const int x = lay::kStageX;
  const int y = lay::kStageY;
  const int w = lay::kStageW;
  const int h = lay::kStageH;
  const bool flash = s.playing && s.in_beat < 140;
  frame(fb, x, y, w, h, s.playing ? (flash ? kInk : kNeon) : kBorder, 3);
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
              kSurface2);
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
  constexpr int kW = lay::kW;
  constexpr int kH = lay::kH;
  constexpr int kPad = lay::kPad;
  fill(fb, 0, 0, kW, kH, kBg);

  paint_header(fb, s);

  const int gear_scale = lay::kGearS >= 56 ? 6 : 5;
  const int gear_px = 7 * gear_scale;
  const int gx = lay::kGearX + (lay::kGearS - gear_px) / 2;
  const int gy = lay::kGearY + (lay::kGearS - gear_px) / 2;
  if (pressed.hit == Hit::kGear) {
    fill_cut(fb, lay::kGearX, lay::kGearY, lay::kGearS, lay::kGearS, kNeon);
    paint_gear(fb, gx, gy, gear_scale, kBg);
  } else {
    frame(fb, lay::kGearX, lay::kGearY, lay::kGearS, lay::kGearS, kNeon, 2);
    paint_gear(fb, gx, gy, gear_scale, kInk);
  }

  // BPM hero (tap-tempo zone). Seven-segment numerals, not scaled 5×7.
  const int bpm_w = hero_width(s.bpm, lay::kBpmCell);
  const int bpm_h = neon::ui::kHeroHeight * lay::kBpmCell;
  const int label_h = 7 * lay::kTapLabelScale;
  const int bpm_x = lay::kTapX + (lay::kTapW - bpm_w) / 2;
  const int bpm_y = lay::kTapY + (lay::kTapH - bpm_h - label_h - 8) / 2;
  if (pressed.hit == Hit::kTap) {
    frame(fb, lay::kTapX, lay::kTapY, lay::kTapW, lay::kTapH, kNeon, 2);
  }
  hero_text(fb, bpm_x, bpm_y, s.bpm, lay::kBpmCell, kInk);
  text_cx(fb, lay::kTapX + lay::kTapW / 2, bpm_y + bpm_h + 8, "TAP TEMPO",
          lay::kTapLabelScale, kMuted);

  paint_btn(fb, lay::kMinusX, lay::kMinusY, lay::kBtnW, lay::kBtnH, "-",
            lay::kBtnScale, pressed.hit == Hit::kMinus, false);
  paint_btn(fb, lay::kPlusX, lay::kPlusY, lay::kBtnW, lay::kBtnH, "+",
            lay::kBtnScale, pressed.hit == Hit::kPlus, false);
  text_cx(fb, lay::kTapX + lay::kTapW / 2,
          lay::kMinusY + (lay::kBtnH - 14) / 2, "BPM", 2, kMuted);

  const bool tr_press = pressed.hit == Hit::kTransport;
  const int tr_cx = lay::kTrX + lay::kTrW / 2;
  const int tr_ty = lay::kTrY + (lay::kTrH - 7 * lay::kTrScale) / 2;
  if (s.playing) {
    fill_cut(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH,
             tr_press ? kInk : kHot);
    frame(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH, kInk, 4);
    text_cx(fb, tr_cx, tr_ty, "STOP", lay::kTrScale, kBg);
  } else {
    fill_cut(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH,
             tr_press ? kInk : kNeon);
    frame(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH, kInk, 4);
    text_cx(fb, tr_cx, tr_ty, "RUN", lay::kTrScale, kBg);
  }

  paint_stage(fb, s);

  const int bar_y = lay::kBarY;
  const int bar_w = kW - 2 * kPad;
  const int bar_h = lay::kBarH;
  fill(fb, kPad, bar_y, bar_w, bar_h, kSurface);
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
    fill(fb, kPad, bar_y, fill_w, bar_h, kNeonDim);
    const int head = 28;
    int hx = kPad + fill_w - head;
    if (hx < kPad) {
      hx = kPad;
    }
    fill(fb, hx, bar_y - 4, head, bar_h + 8, kNeon);
  } else {
    fill(fb, kPad, bar_y, bar_w / 10, bar_h, kBorder);
  }
  for (uint32_t i = 1; i < s.quantum && i < 8; ++i) {
    const int tx = kPad + static_cast<int>((i * bar_w) / s.quantum);
    fill(fb, tx, bar_y - 4, 3, bar_h + 8, kBorder);
  }

  paint_footer(fb, s);
}

void paint_settings(uint16_t* fb, const Snap& s, Touch pressed) {
  using S = neon::MenuModel::Screen;
  constexpr int kW = lay::kW;
  constexpr int kH = lay::kH;
  constexpr int kPad = lay::kPad;
  fill(fb, 0, 0, kW, kH, kBg);

  const bool confirm = g_menu.screen() == S::kConfirm;
  const bool editing_clk = g_menu.screen() == S::kOutputEdit;

  fill(fb, 0, 0, kW, lay::kSetHeadH, kSurface);
  fill(fb, 0, lay::kSetHeadH - 2, kW, 2, kNeonDim);
  if (editing_clk) {
    paint_btn(fb, kPad, 4, lay::kSetCloseW, lay::kSetHeadH - 8, "<", 3,
              pressed.hit == Hit::kBack, false);
    char title[16];
    std::snprintf(title, sizeof(title), "CLK %d", g_menu.output_index() + 1);
    text(fb, kPad + lay::kSetCloseW + 12, (lay::kSetHeadH - 21) / 2, title, 3,
         kInk);
  } else if (confirm) {
    text(fb, kPad, (lay::kSetHeadH - 21) / 2, "REBOOT", 3, kInk);
  } else {
    text(fb, kPad, (lay::kSetHeadH - 21) / 2, "SETTINGS", 3, kInk);
  }
  paint_btn(fb, kW - kPad - lay::kSetCloseW, 4, lay::kSetCloseW,
            lay::kSetHeadH - 8, "X", 3, pressed.hit == Hit::kClose, false);

  if (confirm) {
    text_cx(fb, kW / 2, lay::kSetHeadH + 40, "REBOOT THE MODULE?", 3, kInk);
    text_cx(fb, kW / 2, lay::kSetHeadH + 80, "CLOCK STOPS UNTIL IT RETURNS", 2,
            kMuted);
    const int bw = (kW - 3 * kPad) / 2;
    const int by = kH / 2 + 20;
    const int bh = 88;
    paint_btn(fb, kPad, by, bw, bh, "NO", 4, pressed.hit == Hit::kConfirmNo,
              false);
    fill_cut(fb, kPad + bw + kPad, by, bw, bh,
             pressed.hit == Hit::kConfirmYes ? kInk : kHot);
    frame(fb, kPad + bw + kPad, by, bw, bh, kInk, 4);
    text_cx(fb, kPad + bw + kPad + bw / 2, by + (bh - 28) / 2, "YES", 4, kBg);
    return;
  }

  const int tab_w = kW / kTabCount;
  const int active = tab_index_for(g_menu.screen());
  for (int i = 0; i < kTabCount; ++i) {
    const int x = i * tab_w;
    const bool on = i == active;
    const bool hit = pressed.hit == Hit::kTab && pressed.arg == i;
    fill(fb, x, lay::kSetHeadH, tab_w, lay::kSetTabH,
         hit || on ? kSurface2 : kSurface);
    if (on) {
      fill(fb, x + 8, lay::kSetHeadH + lay::kSetTabH - 4, tab_w - 16, 4, kNeon);
    }
    const int tw = text_width(kTabLabel[i], 2);
    text(fb, x + (tab_w - tw) / 2,
         lay::kSetHeadH + (lay::kSetTabH - 14) / 2, kTabLabel[i], 2,
         on ? kInk : kMuted);
  }

  clamp_scroll(s);
  const int list_y = set_list_y();
  const int n = set_row_count(s);
  const int nudge_w = lay::kSetNudgeW;
  for (int i = 0; i < n; ++i) {
    const int y = list_y + i * lay::kSetRowH - g_scroll_px;
    if (y + lay::kSetRowH <= list_y || y >= kH) {
      continue;
    }
    char label[24] = {};
    char value[72] = {};
    if (g_menu.screen() == S::kNetwork) {
      net_row(s, i, label, sizeof(label), value, sizeof(value));
    } else {
      std::snprintf(label, sizeof(label), "%s", lcd_item_label(i));
      if (g_menu.screen() == S::kSystem &&
          i == neon::MenuModel::kSystemVersionItem) {
        std::snprintf(value, sizeof(value), "%s", s.firmware);
      } else {
        g_menu.item_value(i, value, sizeof(value));
      }
    }
    const bool row_press = pressed.hit == Hit::kRow && pressed.arg == i;
    fill(fb, 0, y, kW, lay::kSetRowH, row_press ? kSurface2 : kBg);
    fill(fb, kPad, y + lay::kSetRowH - 1, kW - 2 * kPad, 1, kSurface);
    const bool nudge = row_has_nudge(i);
    text(fb, kPad, y + (lay::kSetRowH - 14) / 2, label, 2, kMuted);
    if (nudge) {
      const int plus_x = kW - kPad - nudge_w;
      const int minus_x = plus_x - 12 - nudge_w;
      paint_btn(fb, minus_x, y + 6, nudge_w, lay::kSetRowH - 12, "-", 3,
                pressed.hit == Hit::kRowMinus && pressed.arg == i, false);
      paint_btn(fb, plus_x, y + 6, nudge_w, lay::kSetRowH - 12, "+", 3,
                pressed.hit == Hit::kRowPlus && pressed.arg == i, false);
      const int vw = text_width(value, 2);
      text(fb, minus_x - 12 - vw, y + (lay::kSetRowH - 14) / 2, value, 2, kInk);
    } else {
      const int vw = text_width(value, 2);
      text(fb, kW - kPad - vw, y + (lay::kSetRowH - 14) / 2, value, 2, kInk);
    }
  }
}

Touch hit_at_live(int x, int y) {
  if (in_rect(x, y, lay::kGearX, lay::kGearY, lay::kGearS, lay::kGearS)) {
    return {Hit::kGear, 0};
  }
  if (in_rect(x, y, lay::kMinusX, lay::kMinusY, lay::kBtnW, lay::kBtnH)) {
    return {Hit::kMinus, 0};
  }
  if (in_rect(x, y, lay::kPlusX, lay::kPlusY, lay::kBtnW, lay::kBtnH)) {
    return {Hit::kPlus, 0};
  }
  if (in_rect(x, y, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH)) {
    return {Hit::kTransport, 0};
  }
  if (in_rect(x, y, lay::kTapX, lay::kTapY, lay::kTapW, lay::kTapH)) {
    return {Hit::kTap, 0};
  }
  return {Hit::kNone, 0};
}

Touch hit_at_settings(int x, int y, const Snap& s) {
  using S = neon::MenuModel::Screen;
  constexpr int kW = lay::kW;
  constexpr int kPad = lay::kPad;
  if (in_rect(x, y, kW - kPad - lay::kSetCloseW, 4, lay::kSetCloseW,
              lay::kSetHeadH - 8)) {
    return {Hit::kClose, 0};
  }
  if (g_menu.screen() == S::kOutputEdit &&
      in_rect(x, y, kPad, 4, lay::kSetCloseW, lay::kSetHeadH - 8)) {
    return {Hit::kBack, 0};
  }
  if (g_menu.screen() == S::kConfirm) {
    const int bw = (kW - 3 * kPad) / 2;
    const int by = lay::kH / 2 + 20;
    const int bh = 88;
    if (in_rect(x, y, kPad, by, bw, bh)) {
      return {Hit::kConfirmNo, 0};
    }
    if (in_rect(x, y, kPad + bw + kPad, by, bw, bh)) {
      return {Hit::kConfirmYes, 0};
    }
    return {Hit::kNone, 0};
  }
  if (y >= lay::kSetHeadH && y < set_list_y()) {
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
  const int i = (y - list_y + g_scroll_px) / lay::kSetRowH;
  if (i < 0 || i >= set_row_count(s)) {
    return {Hit::kNone, 0};
  }
  if (row_has_nudge(i)) {
    const int plus_x = kW - kPad - lay::kSetNudgeW;
    const int minus_x = plus_x - 12 - lay::kSetNudgeW;
    const int ry = list_y + i * lay::kSetRowH - g_scroll_px;
    if (in_rect(x, y, plus_x, ry + 6, lay::kSetNudgeW, lay::kSetRowH - 12)) {
      return {Hit::kRowPlus, i};
    }
    if (in_rect(x, y, minus_x, ry + 6, lay::kSetNudgeW, lay::kSetRowH - 12)) {
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
    return;
  }
  if (scr == S::kSystem && i == neon::MenuModel::kSystemRebootItem) {
    g_menu.go_section(S::kConfirm);
    return;
  }
  if (scr == S::kSystem && i == neon::MenuModel::kSystemVersionItem) {
    return;
  }
  g_menu.set_cursor(i);
  char val[24] = {};
  g_menu.item_value(i, val, sizeof(val));
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

void fire_settings(Touch t) {
  using S = neon::MenuModel::Screen;
  switch (t.hit) {
    case Hit::kClose:
      close_settings();
      ESP_LOGI(kTag, "settings close");
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
        g_menu.go_section(kTabs[t.arg]);
        g_scroll_px = 0;
      }
      break;
    case Hit::kRow:
      handle_row_tap(t.arg);
      break;
    case Hit::kRowMinus:
      g_menu.set_cursor(t.arg);
      g_menu.nudge_value(-1);
      break;
    case Hit::kRowPlus:
      g_menu.set_cursor(t.arg);
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
  const int64_t now = esp_timer_get_time();
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
          held.hit == Hit::kRowMinus || held.hit == Hit::kRowPlus;
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
    const bool press_now = !g_settings || hit.hit == Hit::kRowMinus ||
                           hit.hit == Hit::kRowPlus;
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
      g_scroll_px += last_y - y;
      last_y = y;
      clamp_scroll(s);
      return {};
    }
  }

  const bool hold =
      (hit.hit == Hit::kMinus || hit.hit == Hit::kPlus ||
       hit.hit == Hit::kRowMinus || hit.hit == Hit::kRowPlus) &&
      hit.hit == held.hit && hit.arg == held.arg;
  if (hold && now - down_us > 400000 && now - last_fire_us > 120000) {
    last_fire_us = now;
    fire(hit);
  }
  return hit;
}

void paint(uint16_t* fb, const Snap& s, Touch pressed) {
  if (g_settings) {
    paint_settings(fb, s, pressed);
  } else {
    paint_face(fb, s, pressed);
  }
}

void lcd_task(void*) {
  if (!halesp::lcd_rgb_init()) {
    ESP_LOGE(kTag, "LCD init failed");
    vTaskDelete(nullptr);
    return;
  }
  if (!halesp::lcd_touch_init()) {
    ESP_LOGW(kTag, "touch init failed — display only");
  }
  g_cfg = neon_config();
  apply_backlight(g_cfg.display_brightness);
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
    const Snap s = snapshot();
    const Touch pressed = poll_touch(s);
    if (dbuf) {
      uint16_t* fb = halesp::lcd_rgb_next_frame();
      paint(fb, s, pressed);
      if (!halesp::lcd_rgb_present()) {
        ESP_LOGW(kTag, "present failed");
      }
      // present() already blocked to the frame boundary (~60 Hz); one
      // tick keeps touch responsive and lands the loop near 30 fps.
      vTaskDelay(pdMS_TO_TICKS(15));
    } else {
      paint(own_fb, s, pressed);
      if (!halesp::lcd_rgb_blit(own_fb, 0, 0, halesp::kLcdW, halesp::kLcdH)) {
        ESP_LOGW(kTag, "blit failed");
      }
      vTaskDelay(pdMS_TO_TICKS(40));  // ~25 Hz
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
