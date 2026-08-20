#include "sdkconfig.h"
#include "tasks.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "board_mac.h"
#include "board_pins.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/lcd_rgb.hpp"
#include "neon/fixed_math.hpp"
#include "neon/gfx/font5x7.hpp"
#include "neon/timeline.hpp"
#include "neon/config/model.hpp"
#include "neon/transport.hpp"
#include "netman/net_manager.h"
#include "provision.h"
#include "wifi.h"

#include <cstdio>
#include <cstring>

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

#if CONFIG_NEON_BOARD_LINKSYNC_TAB5
// Near-black void so the tubes read as neon, not pastel on grey.
constexpr uint16_t kBg = rgb(0, 2, 8);
constexpr uint16_t kSurface = rgb(0, 28, 40);
constexpr uint16_t kSurface2 = rgb(0, 48, 64);
constexpr uint16_t kBorder = rgb(0, 140, 160);
constexpr uint16_t kInk = rgb(255, 255, 255);
constexpr uint16_t kMuted = rgb(0, 200, 210);
constexpr uint16_t kNeon = rgb(0, 255, 255);
constexpr uint16_t kNeonDim = rgb(0, 180, 200);
constexpr uint16_t kSuccess = rgb(48, 255, 160);
constexpr uint16_t kHot = rgb(255, 45, 149);  // STOP tube; cyan stays the live accent
#else
// DESIGN_SYSTEM.md §3.1 — cyan is the only accent.
constexpr uint16_t kBg = rgb(11, 12, 15);
constexpr uint16_t kSurface = rgb(20, 22, 26);
constexpr uint16_t kSurface2 = rgb(28, 32, 40);
constexpr uint16_t kBorder = rgb(42, 46, 56);
constexpr uint16_t kInk = rgb(232, 234, 237);
constexpr uint16_t kMuted = rgb(139, 144, 154);
constexpr uint16_t kNeon = rgb(0, 240, 255);
constexpr uint16_t kNeonDim = rgb(0, 168, 179);
constexpr uint16_t kSuccess = rgb(61, 255, 154);
#endif

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
  static uint32_t last_mbpm = 0;
  if (s.milli_bpm != last_mbpm) {
    ESP_LOGI(kTag, "bpm %s (tl_q32=%llu cfg=%u)", s.bpm,
             static_cast<unsigned long long>(tl.tempo_mpb_q32),
             static_cast<unsigned>(cfg.tempo_milli_bpm));
    last_mbpm = s.milli_bpm;
  }
  return s;
}

void paint_footer(uint16_t* fb, const Snap& s, int foot_y) {
  constexpr int kW = halesp::kLcdW;
  constexpr int kH = halesp::kLcdH;
  constexpr int kPad = 40;
  fill(fb, 0, foot_y, kW, kH - foot_y, kSurface);
  fill(fb, 0, foot_y, kW, 2, kNeonDim);
  const neon::Config& cfg = neon_config();
  if (s.show_ap) {
    text(fb, kPad, foot_y + 16, "SETUP AP", 2, kNeon);
    text(fb, kPad, foot_y + 48, s.ap_ssid[0] ? s.ap_ssid : "LINK-LCD", 3, kInk);
    const char* pass = cfg.ap_pass;
    const int pass_w = text_width(pass, 3);
    text(fb, kW - kPad - pass_w, foot_y + 48, pass, 3, kInk);
    text(fb, kW - kPad - text_width("http://192.168.4.1", 2), foot_y + 16,
         "http://192.168.4.1", 2, kMuted);
  } else if (!s.provisioned) {
    text(fb, kPad, foot_y + 36, "NO WIFI", 3, kMuted);
    text(fb, kPad + text_width("NO WIFI  ", 3), foot_y + 40,
         "SoftAP did not start", 2, kMuted);
  } else {
    const char* net = s.wifi_up ? neon_wifi_current_ssid() : "CONNECTING";
    text(fb, kPad, foot_y + 16, s.wifi_up ? "STA" : "NET", 2, kNeon);
    text(fb, kPad, foot_y + 48, net[0] ? net : "-", 3, kInk);
    text(fb, kW - kPad - text_width("MIDI CLOCK  24 PPQN", 2), foot_y + 40,
         "MIDI CLOCK  24 PPQN", 2, kMuted);
  }
}

void paint_header(uint16_t* fb, const Snap& s, int accent_h) {
  constexpr int kW = halesp::kLcdW;
  constexpr int kPad = 40;
  fill(fb, 0, 0, kW, accent_h, s.playing ? kNeon : kNeonDim);
  text(fb, kPad, 18 + accent_h, s.name, 3, kInk);
  char peers[24];
  std::snprintf(peers, sizeof(peers), "%u", static_cast<unsigned>(s.peers));
  const int peers_w = text_width(peers, 3);
  text(fb, kW - kPad - peers_w, 18 + accent_h, peers, 3, kNeon);
  text(fb, kW - kPad - peers_w - text_width("PEERS ", 2) - 8, 24 + accent_h,
       "PEERS", 2, kMuted);
}

enum class Hit : uint8_t { kNone, kMinus, kPlus, kTransport, kTap };

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
}  // namespace lay

bool in_rect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

Hit hit_at(int x, int y) {
  if (in_rect(x, y, lay::kMinusX, lay::kMinusY, lay::kBtnW, lay::kBtnH)) {
    return Hit::kMinus;
  }
  if (in_rect(x, y, lay::kPlusX, lay::kPlusY, lay::kBtnW, lay::kBtnH)) {
    return Hit::kPlus;
  }
  if (in_rect(x, y, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH)) {
    return Hit::kTransport;
  }
  if (in_rect(x, y, lay::kTapX, lay::kTapY, lay::kTapW, lay::kTapH)) {
    return Hit::kTap;
  }
  return Hit::kNone;
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

void paint_tab5(uint16_t* fb, const Snap& s, Hit pressed) {
  constexpr int kW = lay::kW;
  constexpr int kH = lay::kH;
  constexpr int kPad = lay::kPad;
  fill(fb, 0, 0, kW, kH, kBg);

  // Downbeat flash along the top rail.
  int rail = 6;
  if (s.playing && s.beat == 1 && s.in_beat < 180) {
    rail = 6 + static_cast<int>((180 - s.in_beat) * 10 / 180);
  }
  paint_header(fb, s, rail);

  constexpr int kBpmScale = 18;
  const int bpm_y = 128;
  const int bpm_w = text_width(s.bpm, kBpmScale);
  const int bpm_h = 7 * kBpmScale;
  const int bpm_x = kW / 2 - bpm_w / 2;
  const int plate_x = bpm_x - 16;
  const int plate_y = bpm_y - 12;
  const int plate_w = bpm_w + 32;
  const int plate_h = bpm_h + 24;
  fill(fb, plate_x, plate_y, plate_w, plate_h, kBg);
  const int bpm_pulse =
      s.playing ? 4 + static_cast<int>((255 - (s.in_beat * 255) / 1000) / 40)
                : 3;
  frame(fb, plate_x, plate_y, plate_w, plate_h, kNeon, bpm_pulse);
  text(fb, bpm_x, bpm_y, s.bpm, kBpmScale, kInk);
  text_cx(fb, kW / 2, bpm_y + bpm_h + 18, "TAP  BPM", 3, kMuted);

  paint_btn(fb, lay::kMinusX, lay::kMinusY, lay::kBtnW, lay::kBtnH, "-", 10,
            pressed == Hit::kMinus, false);
  paint_btn(fb, lay::kPlusX, lay::kPlusY, lay::kBtnW, lay::kBtnH, "+", 10,
            pressed == Hit::kPlus, false);
  text_cx(fb, kW / 2, lay::kMinusY + 56, "TEMPO", 2, kMuted);

  // Label is the action, not the current state: RUN while stopped, STOP while
  // running.
  const bool tr_press = pressed == Hit::kTransport;
  if (s.playing) {
    fill_cut(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH,
             tr_press ? kInk : kHot);
    const int pulse = 4 + static_cast<int>((255 - (s.in_beat * 255) / 1000) / 40);
    frame(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH, kInk, pulse);
    text_cx(fb, kW / 2, lay::kTrY + (lay::kTrH - 7 * 8) / 2, "STOP", 8, kBg);
  } else {
    fill_cut(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH,
             tr_press ? kInk : kNeon);
    frame(fb, lay::kTrX, lay::kTrY, lay::kTrW, lay::kTrH, kInk, 4);
    text_cx(fb, kW / 2, lay::kTrY + (lay::kTrH - 7 * 8) / 2, "RUN", 8, kBg);
  }

  const int bar_y = 740;
  const int bar_w = kW - 2 * kPad;
  const int bar_h = 22;
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

  paint_footer(fb, s, kH - 140);
}

void fire(Hit hit) {
  ControlCommand cmd{};
  switch (hit) {
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
    case Hit::kNone:
      return;
  }
  if (!control_queue_push(cmd)) {
    ESP_LOGW(kTag, "control queue full");
    return;
  }
  ESP_LOGI(kTag, "touch %s",
           hit == Hit::kMinus       ? "-1 BPM"
           : hit == Hit::kPlus      ? "+1 BPM"
           : hit == Hit::kTransport ? "toggle"
                                    : "tap tempo");
}

Hit poll_touch() {
  int x = 0;
  int y = 0;
  const bool down = halesp::lcd_touch_poll(&x, &y);
  const int64_t now = esp_timer_get_time();
  static bool was_down = false;
  static Hit held = Hit::kNone;
  static int64_t down_us = 0;
  static int64_t last_fire_us = 0;
  static int log_left = 8;

  if (!down) {
    was_down = false;
    held = Hit::kNone;
    return Hit::kNone;
  }
  const Hit hit = hit_at(x, y);
  if (log_left > 0) {
    ESP_LOGI(kTag, "touch xy=%d,%d hit=%u", x, y, static_cast<unsigned>(hit));
    --log_left;
  }
  if (!was_down) {
    was_down = true;
    held = hit;
    down_us = now;
    last_fire_us = now;
    fire(hit);
    return hit;
  }
  // Hold-repeat on tempo only.
  if ((hit == Hit::kMinus || hit == Hit::kPlus) && hit == held) {
    if (now - down_us > 400000 && now - last_fire_us > 120000) {
      last_fire_us = now;
      fire(hit);
    }
  }
  return hit;
}

#else  // CrowPanel 800×480 — keep the landscape face.

void paint_p4lcd(uint16_t* fb, const Snap& s) {
  constexpr int kW = halesp::kLcdW;
  constexpr int kH = halesp::kLcdH;
  constexpr int kPad = 40;
  fill(fb, 0, 0, kW, kH, kBg);
  paint_header(fb, s, 3);
  fill(fb, kPad, 56, kW - 2 * kPad, 1, kBorder);

  constexpr int kBpmScale = 11;
  text_cx(fb, kW / 2, 78, s.bpm, kBpmScale, kInk);
  text_cx(fb, kW / 2, 78 + 7 * kBpmScale + 12, "BPM", 3, kMuted);

  const char* tr = s.playing ? "RUN" : "STOP";
  constexpr int kTrScale = 4;
  const int tr_w = text_width(tr, kTrScale);
  const int tr_h = 7 * kTrScale;
  const int tr_x = (kW - tr_w) / 2;
  const int tr_y = 214;
  const int chip_x = tr_x - 20;
  const int chip_y = tr_y - 10;
  const int chip_w = tr_w + 40;
  const int chip_h = tr_h + 20;
  if (s.playing) {
    fill(fb, chip_x, chip_y, chip_w, chip_h, kSuccess);
    text(fb, tr_x, tr_y, tr, kTrScale, kBg);
  } else {
    fill(fb, chip_x, chip_y, chip_w, chip_h, kSurface);
    frame(fb, chip_x, chip_y, chip_w, chip_h, kBorder, 2);
    text(fb, tr_x, tr_y, tr, kTrScale, kMuted);
  }

  const int bar_y = 272;
  const int bar_w = kW - 2 * kPad;
  fill(fb, kPad, bar_y, bar_w, 8, kSurface);
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
  fill(fb, kPad, bar_y, s.playing ? fill_w : bar_w / 8, 8,
       s.playing ? kNeon : kBorder);
  for (uint32_t i = 1; i < s.quantum && i < 8; ++i) {
    const int tx = kPad + static_cast<int>((i * bar_w) / s.quantum);
    fill(fb, tx, bar_y - 2, 2, 12, kBorder);
  }

  const int box_y = 300;
  const int box_gap = 16;
  const int box_w = (bar_w - 3 * box_gap) / 4;
  const int box_h = 88;
  for (uint32_t i = 1; i <= 4; ++i) {
    const int bx = kPad + static_cast<int>(i - 1) * (box_w + box_gap);
    const bool on = s.playing && s.beat == i;
    fill(fb, bx, box_y, box_w, box_h, on ? kNeon : kSurface);
    if (!on) {
      frame(fb, bx, box_y, box_w, box_h, kBorder, 2);
    }
    char d[2] = {static_cast<char>('0' + i), 0};
    const int ds = 6;
    text(fb, bx + (box_w - text_width(d, ds)) / 2,
         box_y + (box_h - 7 * ds) / 2, d, ds, on ? kBg : kMuted);
  }

  paint_footer(fb, s, kH - 72);
}

#endif

void paint(uint16_t* fb, Hit pressed) {
  const Snap s = snapshot();
#if CONFIG_NEON_BOARD_LINKSYNC_TAB5
  paint_tab5(fb, s, pressed);
#else
  (void)pressed;
  paint_p4lcd(fb, s);
#endif
}

void lcd_task(void*) {
  if (!halesp::lcd_rgb_init()) {
    ESP_LOGE(kTag, "LCD init failed");
    vTaskDelete(nullptr);
    return;
  }
#if CONFIG_NEON_BOARD_LINKSYNC_TAB5
  if (!halesp::lcd_touch_init()) {
    ESP_LOGW(kTag, "touch init failed — display only");
  }
#endif
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
    Hit pressed = Hit::kNone;
#if CONFIG_NEON_BOARD_LINKSYNC_TAB5
    pressed = poll_touch();
#endif
    paint(fb, pressed);
    if (!halesp::lcd_rgb_blit(fb, 0, 0, halesp::kLcdW, halesp::kLcdH)) {
      ESP_LOGW(kTag, "blit failed");
    }
    vTaskDelay(pdMS_TO_TICKS(40));  // ~25 Hz
  }
}

}  // namespace

void neon_start_lcd_service() {
  xTaskCreatePinnedToCore(lcd_task, "lcd", 8192, nullptr, 3, nullptr, 0);
}

#else

void neon_start_lcd_service() {}

#endif
