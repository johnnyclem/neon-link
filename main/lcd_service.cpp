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
#include "neon/ui/menu_model.hpp"
#include "neon/ui/theme_gen.hpp"
#include "netman/net_manager.h"
#include "provision.h"
#include "wifi.h"

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

void paint_footer(uint16_t* fb, const Snap& s, int foot_y, int pad) {
  constexpr int kW = halesp::kLcdW;
  constexpr int kH = halesp::kLcdH;
  const int compact = (kH - foot_y) < 120;
  const int y1 = foot_y + (compact ? 12 : 16);
  const int y2 = foot_y + (compact ? 40 : 48);
  fill(fb, 0, foot_y, kW, kH - foot_y, kSurface);
  fill(fb, 0, foot_y, kW, 2, kNeonDim);
  const neon::Config& cfg = neon_config();
  if (s.show_ap) {
    text(fb, pad, y1, "SETUP AP", 2, kNeon);
    text(fb, pad, y2, s.ap_ssid[0] ? s.ap_ssid : "LINK-LCD", 3, kInk);
    const char* pass = cfg.ap_pass;
    const int pass_w = text_width(pass, 3);
    text(fb, kW - pad - pass_w, y2, pass, 3, kInk);
    text(fb, kW - pad - text_width("http://192.168.4.1", 2), y1,
         "http://192.168.4.1", 2, kMuted);
  } else if (!s.provisioned) {
    text(fb, pad, foot_y + (compact ? 28 : 36), "NO WIFI", 3, kMuted);
    text(fb, pad + text_width("NO WIFI  ", 3), foot_y + (compact ? 32 : 40),
         "SoftAP did not start", 2, kMuted);
  } else {
    const char* net = s.wifi_up ? neon_wifi_current_ssid() : "CONNECTING";
    text(fb, pad, y1, s.wifi_up ? "STA" : "NET", 2, kNeon);
    text(fb, pad, y2, net[0] ? net : "-", 3, kInk);
    text(fb, kW - pad - text_width("MIDI CLOCK  24 PPQN", 2), y2,
         "MIDI CLOCK  24 PPQN", 2, kMuted);
  }
}

void paint_header(uint16_t* fb, const Snap& s, int accent_h, int pad,
                  int right_reserve) {
  constexpr int kW = halesp::kLcdW;
  fill(fb, 0, 0, kW, accent_h, s.playing ? kNeon : kNeonDim);
  text(fb, pad, 18 + accent_h, s.name, 3, kInk);
  char peers[24];
  std::snprintf(peers, sizeof(peers), "%u", static_cast<unsigned>(s.peers));
  const int peers_w = text_width(peers, 3);
  const int right = kW - pad - right_reserve;
  text(fb, right - peers_w, 18 + accent_h, peers, 3, kNeon);
  text(fb, right - peers_w - text_width("PEERS ", 2) - 8, 24 + accent_h,
       "PEERS", 2, kMuted);
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
// 720×1280 portrait — fat hit targets, hero BPM, phase strip.
namespace lay {
constexpr int kW = halesp::kLcdW;
constexpr int kH = halesp::kLcdH;
constexpr int kPad = 32;
constexpr int kTapX = 48;
constexpr int kTapY = 96;
constexpr int kTapW = kW - 96;
constexpr int kTapH = 220;
constexpr int kBtnW = 228;
constexpr int kBtnH = 156;
constexpr int kMinusX = kPad;
constexpr int kMinusY = 340;
constexpr int kPlusX = kW - kPad - kBtnW;
constexpr int kPlusY = kMinusY;
constexpr int kTrX = kPad;
constexpr int kTrY = 524;
constexpr int kTrW = kW - 2 * kPad;
constexpr int kTrH = 188;
constexpr int kBarY = 740;
constexpr int kBarH = 22;
constexpr int kFootH = 140;
constexpr int kBpmScale = 18;
constexpr int kTapLabelScale = 3;
constexpr int kTrScale = 8;
constexpr int kBtnScale = 10;
constexpr int kGearS = 56;
constexpr int kGearX = kW - kPad - kGearS;
constexpr int kGearY = 18;
constexpr int kSetHeadH = 64;
constexpr int kSetTabH = 64;
constexpr int kSetRowH = 80;
constexpr int kSetCloseW = 80;
constexpr int kSetNudgeW = 80;
}  // namespace lay
#else
// CrowPanel 800×480 landscape — same tubes, same hits, no encoder.
namespace lay {
constexpr int kW = halesp::kLcdW;
constexpr int kH = halesp::kLcdH;
constexpr int kPad = 24;
constexpr int kTapX = kPad;
constexpr int kTapY = 52;
constexpr int kTapW = kW - 2 * kPad;
constexpr int kTapH = 120;
constexpr int kBtnW = 240;
constexpr int kBtnH = 100;
constexpr int kMinusX = kPad;
constexpr int kMinusY = 180;
constexpr int kPlusX = kW - kPad - kBtnW;
constexpr int kPlusY = kMinusY;
constexpr int kTrX = kPad;
constexpr int kTrY = 292;
constexpr int kTrW = kW - 2 * kPad;
constexpr int kTrH = 80;
constexpr int kBarY = 384;
constexpr int kBarH = 16;
constexpr int kFootH = 80;
constexpr int kBpmScale = 12;
constexpr int kTapLabelScale = 2;
constexpr int kTrScale = 6;
constexpr int kBtnScale = 8;
constexpr int kGearS = 44;
constexpr int kGearX = kW - kPad - kGearS;
constexpr int kGearY = 8;
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
        "START/STOP", "BRIGHTNESS",  "BEAT DISP",  "BEAT STYLE",
        "VERSION",    "REBOOT"};
    if (i >= 0 && i < 14) {
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

void paint_face(uint16_t* fb, const Snap& s, Touch pressed) {
  constexpr int kW = lay::kW;
  constexpr int kH = lay::kH;
  constexpr int kPad = lay::kPad;
  fill(fb, 0, 0, kW, kH, kBg);

  int rail = 6;
  if (s.playing && s.beat == 1 && s.in_beat < 180) {
    rail = 6 + static_cast<int>((180 - s.in_beat) * 10 / 180);
  }
  paint_header(fb, s, rail, kPad, lay::kGearS + 12);

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

  const int bpm_h = 7 * lay::kBpmScale;
  const int bpm_w = text_width(s.bpm, lay::kBpmScale);
  const int bpm_x = kW / 2 - bpm_w / 2;
  const int label_h = 7 * lay::kTapLabelScale + 12;
  const int bpm_y = lay::kTapY + (lay::kTapH - bpm_h - label_h) / 2;
  const int plate_x = bpm_x - 16;
  const int plate_y = bpm_y - 12;
  const int plate_w = bpm_w + 32;
  const int plate_h = bpm_h + 24;
  fill(fb, plate_x, plate_y, plate_w, plate_h, kBg);
  const int bpm_pulse =
      s.playing ? 4 + static_cast<int>((255 - (s.in_beat * 255) / 1000) / 40)
                : 3;
  frame(fb, plate_x, plate_y, plate_w, plate_h, kNeon, bpm_pulse);
  text(fb, bpm_x, bpm_y, s.bpm, lay::kBpmScale, kInk);
  text_cx(fb, kW / 2, bpm_y + bpm_h + 10, "TAP  BPM", lay::kTapLabelScale,
          kMuted);

  paint_btn(fb, lay::kMinusX, lay::kMinusY, lay::kBtnW, lay::kBtnH, "-",
            lay::kBtnScale, pressed.hit == Hit::kMinus, false);
  paint_btn(fb, lay::kPlusX, lay::kPlusY, lay::kBtnW, lay::kBtnH, "+",
            lay::kBtnScale, pressed.hit == Hit::kPlus, false);
  text_cx(fb, kW / 2, lay::kMinusY + (lay::kBtnH - 14) / 2, "TEMPO", 2,
          kMuted);

  const bool tr_press = pressed.hit == Hit::kTransport;
  const int tr_ty = lay::kTrY + (lay::kTrH - 7 * lay::kTrScale) / 2;
  if (s.playing) {
    fill_cut(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH,
             tr_press ? kInk : kHot);
    const int pulse = 4 + static_cast<int>((255 - (s.in_beat * 255) / 1000) / 40);
    frame(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH, kInk, pulse);
    text_cx(fb, kW / 2, tr_ty, "STOP", lay::kTrScale, kBg);
  } else {
    fill_cut(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH,
             tr_press ? kInk : kNeon);
    frame(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH, kInk, 4);
    text_cx(fb, kW / 2, tr_ty, "RUN", lay::kTrScale, kBg);
  }

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

  paint_footer(fb, s, kH - lay::kFootH, kPad);
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
    if (was_down && g_settings && !dragging) {
      const bool already =
          held.hit == Hit::kRowMinus || held.hit == Hit::kRowPlus;
      if (!already) {
        released = hit_at(down_x, down_y, s);
        fire(released);
      }
    }
    was_down = false;
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
  auto* fb = static_cast<uint16_t*>(heap_caps_malloc(
      static_cast<size_t>(halesp::kLcdW) * halesp::kLcdH * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (fb == nullptr) {
    ESP_LOGE(kTag, "no PSRAM for %dx%d framebuffer", halesp::kLcdW,
             halesp::kLcdH);
    vTaskDelete(nullptr);
    return;
  }
  ESP_LOGI(kTag, "live panel %dx%d touch=%d", halesp::kLcdW, halesp::kLcdH,
           halesp::lcd_touch_ok() ? 1 : 0);
  for (;;) {
    const Snap s = snapshot();
    const Touch pressed = poll_touch(s);
    paint(fb, s, pressed);
    if (!halesp::lcd_rgb_blit(fb, 0, 0, halesp::kLcdW, halesp::kLcdH)) {
      ESP_LOGW(kTag, "blit failed");
    }
    vTaskDelay(pdMS_TO_TICKS(40));  // ~25 Hz
  }
}

}  // namespace

void neon_start_lcd_service() {
  xTaskCreatePinnedToCore(lcd_task, "lcd", 12288, nullptr, 3, nullptr, 0);
}

#else

void neon_start_lcd_service() {}

#endif
