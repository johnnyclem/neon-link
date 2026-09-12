#include "sdkconfig.h"
#include "tasks.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "board_mac.h"
#include "board_pins.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/i2c_bus.hpp"
#include "neon/config/model.hpp"
#include "neon/fixed_math.hpp"
#include "neon/gfx/font5x7.hpp"
#include "neon/input/bytebutton.hpp"
#include "neon/timeline.hpp"
#include "neon/transport.hpp"
#include "neon/ui/idle_dimmer.hpp"
#include "netman/net_manager.h"
#include "wifi.h"

#include "driver/gpio.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#if CONFIG_NEON_BOARD_LINKSYNC_C3OLED

// Uber-minimal 72×40 status for the ACEIRMC / Super Mini ESP32-C3 stamp.
// SSD1306-compatible 0.42" panel over I2C (SDA=5 SCL=6). BOOT (GPIO9)
// still cycles pages / long-press toggles transport. Optional M5 Unit
// ByteButton (U192) @ 0x47 shares that I2C bus for BPM, transport, and
// settings. MIDI stays on UART GPIO20/21 — do not daisy-chain the
// SAM2695 off the ByteButton's second Grove (that jack is I2C too).

namespace {

const char* kTag = "c3oled";

constexpr int kW = 72;
constexpr int kH = 40;
constexpr int kPages = kH / 8;
constexpr size_t kFb = kW * kPages;  // 360
constexpr uint8_t kColOff = 28;      // EastRising 0.42" window in 128 GDDRAM

uint8_t g_fb[kFb];
uint8_t g_addr = 0x3C;
bool g_ok = false;

enum class Page : uint8_t { Live = 0, Net = 1, Setup = 2, Ctrl = 3 };
constexpr int kPageCount = 4;
std::atomic<uint8_t> g_page_u8{static_cast<uint8_t>(Page::Live)};
std::atomic<uint8_t> g_ctrl_row{0};
std::atomic<bool> g_bb_activity{false};
std::atomic<bool> g_bb_ok{false};

Page current_page() {
  return static_cast<Page>(g_page_u8.load(std::memory_order_relaxed));
}

void set_page(Page p) {
  g_page_u8.store(static_cast<uint8_t>(p), std::memory_order_relaxed);
}

void cycle_page() {
  const int next =
      (static_cast<int>(current_page()) + 1) % kPageCount;
  set_page(static_cast<Page>(next));
  ESP_LOGI(kTag, "page %d", next);
}

// ---- I2C SSD1306 -------------------------------------------------------

bool cmd_list(const uint8_t* cmds, size_t n) {
  uint8_t buf[33];
  if (n == 0 || n > 32) {
    return false;
  }
  buf[0] = 0x00;
  std::memcpy(buf + 1, cmds, n);
  return halesp::i2c_write(g_addr, buf, n + 1, 50);
}

bool data_write(const uint8_t* d, size_t n) {
  uint8_t pkt[1 + 72];
  pkt[0] = 0x40;
  size_t off = 0;
  while (off < n) {
    const size_t chunk = (n - off) > 72 ? 72 : (n - off);
    std::memcpy(pkt + 1, d + off, chunk);
    if (!halesp::i2c_write(g_addr, pkt, 1 + chunk, 80)) {
      return false;
    }
    off += chunk;
  }
  return true;
}

bool ssd1306_72x40_init() {
  // U8g2 SSD1306_72X40_ER. Multiplex 40-1, column window starts at 28.
  const uint8_t cmds[] = {
      0xAE, 0xD5, 0x80, 0xA8, 0x27, 0xD3, 0x00, 0x40, 0x8D, 0x14,
      0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xAF, 0xD9, 0x22,
      0xDB, 0x20, 0x2E, 0xA4, 0xA6, 0xAF,
  };
  return cmd_list(cmds, sizeof(cmds));
}

bool flush() {
  if (!g_ok) {
    return false;
  }
  const uint8_t col[3] = {0x21, kColOff,
                          static_cast<uint8_t>(kColOff + kW - 1)};
  const uint8_t page[3] = {0x22, 0, static_cast<uint8_t>(kPages - 1)};
  if (!cmd_list(col, 3) || !cmd_list(page, 3)) {
    return false;
  }
  return data_write(g_fb, kFb);
}

// Runtime contrast (0x81) — the init blob bakes 0xAF in; this is what
// makes display_brightness and the idle dimmer real on this panel.
bool set_contrast(uint8_t level) {
  const uint8_t cmds[] = {0x81, level};
  return cmd_list(cmds, sizeof(cmds));
}

bool set_display_on(bool on) {
  const uint8_t cmd = on ? 0xAF : 0xAE;
  return cmd_list(&cmd, 1);
}

void fb_clear() { std::memset(g_fb, 0, sizeof(g_fb)); }

void px(int x, int y, bool on) {
  if (x < 0 || y < 0 || x >= kW || y >= kH) {
    return;
  }
  const int i = (y / 8) * kW + x;
  const uint8_t bit = static_cast<uint8_t>(1u << (y & 7));
  if (on) {
    g_fb[i] |= bit;
  } else {
    g_fb[i] &= static_cast<uint8_t>(~bit);
  }
}

void fill(int x, int y, int w, int h, bool on) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      px(xx, yy, on);
    }
  }
}

void hline(int x, int y, int w, bool on) { fill(x, y, w, 1, on); }

int text(int x, int y, const char* s, int scale) {
  if (s == nullptr || scale < 1) {
    return x;
  }
  int cx = x;
  for (const char* p = s; *p != '\0'; ++p) {
    const uint8_t* g = neon::gfx::glyph5x7(*p);
    for (int col = 0; col < 5; ++col) {
      const uint8_t bits = g[col];
      for (int row = 0; row < 7; ++row) {
        if ((bits >> row) & 1u) {
          fill(cx + col * scale, y + row * scale, scale, scale, true);
        }
      }
    }
    cx += 6 * scale;
  }
  return cx;
}

int text_w(const char* s, int scale) {
  if (s == nullptr || scale < 1) {
    return 0;
  }
  return static_cast<int>(std::strlen(s)) * 6 * scale;
}

void text_cx(int cx, int y, const char* s, int scale) {
  text(cx - text_w(s, scale) / 2, y, s, scale);
}

void circle(int cx, int cy, int r, bool fill_on) {
  for (int y = -r; y <= r; ++y) {
    for (int x = -r; x <= r; ++x) {
      if (x * x + y * y <= r * r) {
        px(cx + x, cy + y, fill_on);
      }
    }
  }
}

void ring(int cx, int cy, int r) {
  circle(cx, cy, r, true);
  circle(cx, cy, r - 1, false);
}

void scan_bus() {
  for (uint8_t a = 0x08; a < 0x78; ++a) {
    if (halesp::i2c_probe(a, 5)) {
      ESP_LOGI(kTag, "I2C ACK 0x%02x", a);
    }
  }
}

bool panel_init() {
  if (halesp::i2c_bus() == nullptr) {
    if (!halesp::i2c_bus_init(kPinI2cSda, kPinI2cScl)) {
      ESP_LOGW(kTag, "I2C init failed SDA=%d SCL=%d", kPinI2cSda, kPinI2cScl);
      return false;
    }
  }
  vTaskDelay(pdMS_TO_TICKS(50));
  scan_bus();

  const uint8_t candidates[] = {0x3C, 0x3D};
  for (uint8_t a : candidates) {
    if (!halesp::i2c_probe(a, 8)) {
      continue;
    }
    g_addr = a;
    if (ssd1306_72x40_init()) {
      ESP_LOGI(kTag, "SSD1306 72x40 @ 0x%02x", a);
      g_ok = true;
      return true;
    }
    ESP_LOGW(kTag, "0x%02x ACK but init failed", a);
  }
  ESP_LOGW(kTag, "no OLED on SDA=%d SCL=%d", kPinI2cSda, kPinI2cScl);
  return false;
}

// ---- pages -------------------------------------------------------------

struct Snap {
  uint32_t milli_bpm = 120000;
  uint32_t phase = 0;
  uint32_t quantum = 4;
  uint32_t beat = 1;
  uint32_t peers = 0;
  bool playing = false;
  bool provisioned = false;
  bool wifi_up = false;
  bool setup_ap = false;
  char bpm[12] = {};
  char name[24] = {};
  char ssid[33] = {};
  char ap_ssid[33] = {};
  char ap_pass[65] = {};
  char ip[16] = {};
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
  s.provisioned = neon_wifi_has_credentials();
  s.wifi_up = neon_wifi_sta_got_ip();
  s.setup_ap = netman::ap_is_up();
#if CONFIG_NEON_SYNC
  const char* name = cfg.device_name[0] ? cfg.device_name : "near-c3";
#else
  const char* name = cfg.device_name[0] ? cfg.device_name : "link-c3";
#endif
  std::snprintf(s.name, sizeof(s.name), "%s", name);
  std::snprintf(s.bpm, sizeof(s.bpm), "%u.%u",
                static_cast<unsigned>(s.milli_bpm / 1000u),
                static_cast<unsigned>((s.milli_bpm / 100u) % 10u));
  std::snprintf(s.ssid, sizeof(s.ssid), "%s", neon_wifi_current_ssid());
  std::snprintf(s.ap_pass, sizeof(s.ap_pass), "%s", cfg.ap_pass);
  if (s.setup_ap && netman::ap_ssid()[0] != '\0') {
    std::snprintf(s.ap_ssid, sizeof(s.ap_ssid), "%s", netman::ap_ssid());
  } else {
    uint8_t mac[6] = {};
    neon_read_unit_mac(mac);
    neon::ap_ssid_for(cfg, mac, s.ap_ssid, sizeof(s.ap_ssid));
  }
  netman::primary_ip(s.ip, sizeof(s.ip));
  return s;
}

void paint_live(const Snap& s) {
  // Hero tempo, 2× 5×7 — "120.0" is 5 glyphs × 12 px = 60.
  text_cx(kW / 2, 0, s.bpm, 2);

  const int n = static_cast<int>(s.quantum > 8 ? 8 : s.quantum);
  const int span = n * 9;
  const int x0 = (kW - span) / 2 + 4;
  for (int i = 0; i < n; ++i) {
    const int cx = x0 + i * 9;
    const bool cur = (static_cast<uint32_t>(i) + 1u) == s.beat;
    if (s.playing && cur) {
      circle(cx, 20, 3, true);
    } else {
      ring(cx, 20, 3);
    }
  }

  char line[13] = {};
  std::snprintf(line, sizeof(line), "%s %up", s.playing ? "PLAY" : "STOP",
                static_cast<unsigned>(s.peers));
  text(0, 26, line, 1);

  const char* net = !s.provisioned ? "SETUP" : s.wifi_up ? "WIFI" : "WAIT";
  const int nw = text_w(net, 1);
  text(kW - nw, 26, net, 1);

  text(0, 33, s.name, 1);
}

void paint_net(const Snap& s) {
  text(0, 0, "NET", 1);
  hline(0, 8, kW, true);
  if (!s.provisioned) {
    text(0, 11, "no wifi", 1);
    text(0, 20, "see SETUP", 1);
    text(0, 29, "BOOT click", 1);
    return;
  }
  const char* ssid = s.ssid[0] ? s.ssid : "(joining)";
  text(0, 11, ssid, 1);
  text(0, 20, s.ip[0] ? s.ip : "...", 1);
  char rssi[13] = {};
  const int8_t db = neon_wifi_rssi();
  if (s.wifi_up && db != 0) {
    std::snprintf(rssi, sizeof(rssi), "%ddBm %up", static_cast<int>(db),
                  static_cast<unsigned>(s.peers));
  } else {
    std::snprintf(rssi, sizeof(rssi), "wait %up",
                  static_cast<unsigned>(s.peers));
  }
  text(0, 29, rssi, 1);
}

void paint_setup(const Snap& s) {
  text(0, 0, s.ap_ssid, 1);
  text(0, 11, s.ap_pass[0] ? s.ap_pass : "(open)", 1);
  text(0, 20, "192.168.4.1", 1);
  char rf[16] = {};
  const int db = static_cast<int>(netman::wifi_tx_qdBm());
  int n = netman::wifi_nearby_count();
  if (n > 9) {
    n = 9;
  }
  if (db > 0 && n >= 0) {
    std::snprintf(rf, sizeof(rf), "%d.%ddBm %dn", db / 4,
                  ((db % 4) * 25) / 10, n);
  } else if (db > 0) {
    std::snprintf(rf, sizeof(rf), "%d.%ddBm", db / 4, ((db % 4) * 25) / 10);
  } else {
    std::snprintf(rf, sizeof(rf), "rf ?");
  }
  text(0, 29, rf, 1);
}

const char* clock_src_label(neon::ClockSource s) {
  switch (s) {
    case neon::ClockSource::kLinkMaster:
      return "LINK";
    case neon::ClockSource::kMidiMaster:
      return "MIDI";
    case neon::ClockSource::kExternalMaster:
      return "EXT";
    default:
      return "AUTO";
  }
}

void paint_ctrl(const Snap& s) {
  (void)s;
  const neon::Config& cfg = neon_config();
  const int row = static_cast<int>(g_ctrl_row.load(std::memory_order_relaxed));
  char line[13] = {};
  std::snprintf(line, sizeof(line), "%sCLK %s", row == 0 ? ">" : " ",
                cfg.midi_clock_out ? "ON" : "OFF");
  text(0, 0, line, 1);
  std::snprintf(line, sizeof(line), "%sSRC %s", row == 1 ? ">" : " ",
                clock_src_label(cfg.clock_source));
  text(0, 10, line, 1);
  std::snprintf(line, sizeof(line), "%sQ   %u", row == 2 ? ">" : " ",
                static_cast<unsigned>(cfg.quantum_beats));
  text(0, 20, line, 1);
  text(0, 31, g_bb_ok.load(std::memory_order_relaxed) ? "B0-7 pad" : "no pad",
       1);
}

void paint_splash() {
  fb_clear();
  text_cx(kW / 2, 6, "NEON", 2);
#if CONFIG_NEON_SYNC
  text_cx(kW / 2, 28, "nearby", 1);
#else
  text_cx(kW / 2, 28, "link-c3", 1);
#endif
  flush();
}

void paint(const Snap& s) {
  fb_clear();
  switch (current_page()) {
    case Page::Live:
      paint_live(s);
      break;
    case Page::Net:
      paint_net(s);
      break;
    case Page::Setup:
      paint_setup(s);
      break;
    case Page::Ctrl:
      paint_ctrl(s);
      break;
  }
  flush();
}

// ---- M5 Unit ByteButton (U192) @ 0x47 ----------------------------------
//
// Port A Grove is GND/5V/SDA/SCL. Same I2C bus as the OLED. STM32 wants
// write-STOP-read (same as Unit Encoder). LED RGB888 is BGR.

constexpr uint8_t kBbAddr = 0x47;
constexpr uint8_t kBbRegStatus = 0x00;
constexpr uint8_t kBbRegBright = 0x10;
constexpr uint8_t kBbRegMode = 0x19;
constexpr uint8_t kBbRegRgb = 0x20;
constexpr int kBbTimeoutMs = 15;

std::atomic<uint8_t> g_bb_down{0};
uint8_t g_bb_invert = 0;  // 0xFF if idle reads as all-ones

void push_ctrl(ControlCommand::Kind kind, int32_t arg = 0) {
  ControlCommand cmd{};
  cmd.kind = kind;
  cmd.arg = arg;
  control_queue_push(cmd);
}

void apply_cfg(const neon::Config& cfg) {
  neon_config_apply(cfg);
}

void toggle_midi_clk() {
  neon::Config cfg = neon_config();
  cfg.midi_clock_out = cfg.midi_clock_out ? 0 : 1;
  apply_cfg(cfg);
  ESP_LOGI(kTag, "midi clk out %s", cfg.midi_clock_out ? "ON" : "OFF");
}

void cycle_clock_src() {
  neon::Config cfg = neon_config();
  switch (cfg.clock_source) {
    case neon::ClockSource::kAuto:
      cfg.clock_source = neon::ClockSource::kLinkMaster;
      break;
    case neon::ClockSource::kLinkMaster:
      cfg.clock_source = neon::ClockSource::kMidiMaster;
      break;
    default:
      cfg.clock_source = neon::ClockSource::kAuto;
      break;
  }
  apply_cfg(cfg);
  ESP_LOGI(kTag, "clock src %s", clock_src_label(cfg.clock_source));
}

void cycle_quantum(int delta) {
  neon::Config cfg = neon_config();
  static constexpr uint32_t kQs[] = {1, 2, 4, 8};
  int idx = 2;
  for (int i = 0; i < 4; ++i) {
    if (cfg.quantum_beats == kQs[i]) {
      idx = i;
      break;
    }
  }
  idx = (idx + delta + 4) % 4;
  cfg.quantum_beats = kQs[idx];
  apply_cfg(cfg);
  ESP_LOGI(kTag, "quantum %u", static_cast<unsigned>(cfg.quantum_beats));
}

void nudge_ctrl_row(int delta) {
  const int row = static_cast<int>(g_ctrl_row.load(std::memory_order_relaxed));
  if (row == 0) {
    toggle_midi_clk();
  } else if (row == 1) {
    cycle_clock_src();
  } else {
    cycle_quantum(delta >= 0 ? 1 : -1);
  }
}

void move_ctrl_row(int delta) {
  int row = static_cast<int>(g_ctrl_row.load(std::memory_order_relaxed));
  row = (row + delta + 3) % 3;
  g_ctrl_row.store(static_cast<uint8_t>(row), std::memory_order_relaxed);
}

bool bb_read_mask(uint8_t* mask) {
  const uint8_t reg = kBbRegStatus;
  uint8_t v = 0;
  if (!halesp::i2c_write_stop_read(kBbAddr, &reg, 1, &v, 1, kBbTimeoutMs)) {
    return false;
  }
  *mask = static_cast<uint8_t>(v ^ g_bb_invert);
  return true;
}

bool bb_write(uint8_t reg, const uint8_t* data, size_t n) {
  uint8_t buf[32];
  if (n + 1 > sizeof(buf)) {
    return false;
  }
  buf[0] = reg;
  if (n > 0 && data != nullptr) {
    std::memcpy(buf + 1, data, n);
  }
  return halesp::i2c_write(kBbAddr, buf, n + 1, kBbTimeoutMs);
}

void bb_led_bgr(uint8_t* p, uint8_t r, uint8_t g, uint8_t b) {
  p[0] = b;
  p[1] = g;
  p[2] = r;
}

void bb_paint_leds(bool playing, uint32_t beat, bool clk_out, uint8_t down) {
  uint8_t rgb[27] = {};
  auto lit = [&](int i, uint8_t r, uint8_t g, uint8_t b) {
    if (down & static_cast<uint8_t>(1u << i)) {
      r = static_cast<uint8_t>(r < 200 ? r + 40 : 255);
      g = static_cast<uint8_t>(g < 200 ? g + 24 : 255);
      b = static_cast<uint8_t>(b < 200 ? b + 16 : 255);
    }
    bb_led_bgr(rgb + i * 3, r, g, b);
  };
  // B0 play: orange running, dim graphite stopped.
  if (playing) {
    lit(0, 64, 38, 12);
  } else {
    lit(0, 8, 8, 8);
  }
  lit(1, 12, 10, 4);   // B1 tap
  lit(2, 20, 8, 4);    // B2 -1
  lit(3, 20, 8, 4);    // B3 +1
  lit(4, 28, 10, 4);   // B4 -10
  lit(5, 28, 10, 4);   // B5 +10
  switch (current_page()) {
    case Page::Live:
      lit(6, 48, 32, 6);
      break;
    case Page::Net:
      lit(6, 6, 28, 28);
      break;
    case Page::Setup:
      lit(6, 48, 40, 8);
      break;
    case Page::Ctrl:
      lit(6, 48, 8, 28);
      break;
  }
  if (clk_out) {
    lit(7, 6, 36, 36);
  } else {
    lit(7, 6, 6, 6);
  }
  // LED8 (centre): magenta on 1, orange on other beats, off if stopped.
  if (playing && beat == 1) {
    bb_led_bgr(rgb + 24, 48, 8, 28);
  } else if (playing) {
    bb_led_bgr(rgb + 24, 40, 24, 6);
  }
  (void)bb_write(kBbRegRgb, rgb, sizeof(rgb));
}

bool bb_probe() {
  if (!halesp::i2c_probe(kBbAddr, 20)) {
    return false;
  }
  uint8_t raw = 0;
  const uint8_t reg = kBbRegStatus;
  if (!halesp::i2c_write_stop_read(kBbAddr, &reg, 1, &raw, 1, kBbTimeoutMs)) {
    ESP_LOGW(kTag, "ByteButton ACK 0x47 but status read failed");
    return false;
  }
  // Resting mask is 0x00 (1=pressed). If the pad comes up all-ones, invert.
  if (raw == 0xFF) {
    g_bb_invert = 0xFF;
  }
  uint8_t bright[9];
  std::memset(bright, 48, sizeof(bright));
  (void)bb_write(kBbRegBright, bright, sizeof(bright));
  const uint8_t mode = 0;  // user-defined RGB
  (void)bb_write(kBbRegMode, &mode, 1);
  g_bb_ok.store(true, std::memory_order_relaxed);
  ESP_LOGI(kTag, "ByteButton U192 @ 0x47 invert=0x%02x", g_bb_invert);
  bb_paint_leds(false, 1, neon_config().midi_clock_out != 0, 0);
  return true;
}

void bb_dispatch(const neon::ByteButtonDecoder::Event& ev) {
  using Kind = neon::ByteButtonDecoder::Event::Kind;
  if (ev.kind == Kind::kNone) {
    return;
  }
  g_bb_activity.store(true, std::memory_order_relaxed);
  const bool ctrl = current_page() == Page::Ctrl;

  if (ev.kind == Kind::kCombo) {
    if (ev.a == 0 && ev.b == 1) {
      push_ctrl(ControlCommand::Kind::kStopNow);
      ESP_LOGI(kTag, "combo B0+B1 stop now");
    } else if (ev.a == 0 && ev.b == 6) {
      push_ctrl(ControlCommand::Kind::kPlayNow);
      ESP_LOGI(kTag, "combo B0+B6 play now");
    }
    return;
  }

  const bool bpm = ev.kind == Kind::kPress || ev.kind == Kind::kRepeat;
  if (bpm && !ctrl) {
    int32_t n = 0;
    if (ev.a == 2) {
      n = -1;
    } else if (ev.a == 3) {
      n = 1;
    } else if (ev.a == 4) {
      n = -10;
    } else if (ev.a == 5) {
      n = 10;
    }
    if (n != 0) {
      push_ctrl(ControlCommand::Kind::kNudgeTempo, n);
      return;
    }
  }

  if (ctrl && (ev.kind == Kind::kPress || ev.kind == Kind::kRepeat)) {
    if (ev.a == 2) {
      nudge_ctrl_row(-1);
      return;
    }
    if (ev.a == 3) {
      nudge_ctrl_row(1);
      return;
    }
    if (ev.a == 4) {
      move_ctrl_row(-1);
      return;
    }
    if (ev.a == 5) {
      move_ctrl_row(1);
      return;
    }
  }

  if (ev.kind == Kind::kShort) {
    switch (ev.a) {
      case 0:
        push_ctrl(ControlCommand::Kind::kToggle);
        break;
      case 1:
        push_ctrl(ControlCommand::Kind::kTapTempo);
        break;
      case 6:
        cycle_page();
        break;
      case 7:
        toggle_midi_clk();
        break;
      default:
        break;
    }
    return;
  }

  if (ev.kind == Kind::kHold) {
    switch (ev.a) {
      case 0:
        push_ctrl(ControlCommand::Kind::kStopNow);
        break;
      case 6:
        cycle_quantum(1);
        break;
      case 7:
        cycle_clock_src();
        break;
      default:
        break;
    }
  }
}

void bb_task(void*) {
  neon::ByteButtonDecoder dec;
  bool last_playing = false;
  uint32_t last_beat = 0;
  uint8_t last_clk = 0xFF;
  uint8_t last_page = 0xFF;
  uint8_t last_down = 0xFF;
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    uint8_t mask = 0;
    if (bb_read_mask(&mask)) {
      g_bb_down.store(mask, std::memory_order_relaxed);
      dec.feed(mask, esp_timer_get_time());
      for (;;) {
        const auto ev = dec.take();
        if (ev.kind == neon::ByteButtonDecoder::Event::Kind::kNone) {
          break;
        }
        bb_dispatch(ev);
      }
    }

    neon::TimelineSnapshot tl{};
    timeline_bus().read(tl);
    const bool playing = tl.playing != 0;
    const uint32_t beat = neon::beat_number(
        neon::phase_milli_beats(tl, esp_timer_get_time()),
        tl.quantum_beats != 0 ? tl.quantum_beats : 4);
    const uint8_t clk = neon_config().midi_clock_out;
    const uint8_t page = g_page_u8.load(std::memory_order_relaxed);
    const uint8_t down = g_bb_down.load(std::memory_order_relaxed);
    if (playing != last_playing || beat != last_beat || clk != last_clk ||
        page != last_page || down != last_down) {
      last_playing = playing;
      last_beat = beat;
      last_clk = clk;
      last_page = page;
      last_down = down;
      bb_paint_leds(playing, beat, clk != 0, down);
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(5));
  }
}

// ---- BOOT button -------------------------------------------------------

void button_init() {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ull << static_cast<unsigned>(kPinEncSw);
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io);
}

bool button_down() {
  return gpio_get_level(static_cast<gpio_num_t>(kPinEncSw)) == 0;
}

void oled_task(void*) {
  button_init();
  if (!panel_init()) {
    ESP_LOGW(kTag, "OLED missing; BOOT still cycles (no glass)");
  } else {
    paint_splash();
    vTaskDelay(pdMS_TO_TICKS(700));
  }
  if (bb_probe()) {
    xTaskCreatePinnedToCore(bb_task, "c3bb", 2560, nullptr, 4, nullptr,
                            kNeonCoreApp);
  } else {
    ESP_LOGI(kTag, "no ByteButton @ 0x47 (OLED bus GPIO5/6)");
  }

  bool held = false;
  int64_t down_us = 0;
  bool long_fired = false;
  constexpr int64_t kLongUs = 700000;
  constexpr int64_t kDebounceUs = 30000;

  // Land on SETUP until WiFi is stored so the password is the first thing
  // a first-boot user sees.
  if (!neon_wifi_has_credentials()) {
    set_page(Page::Setup);
  }

  neon::ui::IdleDimmer dimmer;
  dimmer.note_activity(esp_timer_get_time());
  bool swallow_press = false;
  int applied_contrast = -1;
  bool display_on = true;

  for (;;) {
    const int64_t now = esp_timer_get_time();
    {
      const neon::Config& live = neon_config();
      dimmer.configure(live.display_dim_s, live.display_dim_level);
    }
    const bool down = button_down();
    if (down || g_bb_activity.exchange(false, std::memory_order_relaxed)) {
      dimmer.note_activity(now);
    }
    if (swallow_press) {
      // This press only woke the blanked panel; drop it whole.
      if (!down) {
        swallow_press = false;
      }
      vTaskDelay(pdMS_TO_TICKS(80));
      continue;
    }
    if (!display_on && down) {
      // Wake press: light the panel now, act on nothing.
      swallow_press = true;
      held = false;
      if (g_ok) {
        set_display_on(true);
        display_on = true;
      }
      vTaskDelay(pdMS_TO_TICKS(80));
      continue;
    }
    if (down && !held) {
      held = true;
      down_us = now;
      long_fired = false;
    } else if (down && held && !long_fired && now - down_us >= kLongUs) {
      long_fired = true;
      ControlCommand cmd{};
      cmd.kind = ControlCommand::Kind::kToggle;
      control_queue_push(cmd);
      ESP_LOGI(kTag, "long press: toggle transport");
    } else if (!down && held) {
      const int64_t dt = now - down_us;
      held = false;
      if (!long_fired && dt >= kDebounceUs) {
        cycle_page();
      }
    }

    const Snap snap = snapshot();
    dimmer.set_playing(snap.playing);
    const int eff = dimmer.apply(neon_config().display_brightness, now);
    if (g_ok) {
      if (eff == 0 && display_on) {
        set_display_on(false);
        display_on = false;
      } else if (eff != 0 && !display_on) {
        set_display_on(true);
        display_on = true;
      }
      if (eff != 0 && eff != applied_contrast) {
        set_contrast(static_cast<uint8_t>(eff));
        applied_contrast = eff;
      }
    }
    if (display_on) {
      paint(snap);
    }
    neon_config_flush(now);
    vTaskDelay(pdMS_TO_TICKS(80 + dimmer.frame_interval_hint_ms(now)));
  }
}

}  // namespace

void neon_start_c3oled_service() {
  xTaskCreatePinnedToCore(oled_task, "c3oled", 4096, nullptr, 3, nullptr,
                          kNeonCoreApp);
}

#else

void neon_start_c3oled_service() {}

#endif
