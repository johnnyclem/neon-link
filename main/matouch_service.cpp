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
#include "halesp/midi_uart.hpp"
#include "neon/fixed_math.hpp"
#include "neon/gfx/font5x7.hpp"
#include "neon/timeline.hpp"
#include "neon/transport.hpp"
#include "neon/config/model.hpp"
#include "neon/ui/menu_model.hpp"
#include "neon/ui/idle_dimmer.hpp"
#include "neon/ui/palettes_gen.hpp"
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

struct Pal {
  uint16_t bg;
  uint16_t ring;
  uint16_t surface;
  uint16_t surface2;
  uint16_t neon;
  uint16_t neon_dim;
  uint16_t ink;
  uint16_t muted;
  uint16_t hot;
  uint16_t hero;
};

Pal pal{};

Pal make_pal(neon::ColorTheme theme) {
  const neon::ui::ColorPalette& s =
      neon::ui::color_palette(static_cast<uint8_t>(theme));
  Pal p;
  p.bg = rgb(s.bg.r, s.bg.g, s.bg.b);
  p.ring = rgb(s.surface.r, s.surface.g, s.surface.b);
  p.surface = rgb(s.surface.r, s.surface.g, s.surface.b);
  p.surface2 = rgb(s.surface2.r, s.surface2.g, s.surface2.b);
  p.neon = rgb(s.neon.r, s.neon.g, s.neon.b);
  p.neon_dim = rgb(s.neon_dim.r, s.neon_dim.g, s.neon_dim.b);
  p.ink = rgb(s.text.r, s.text.g, s.text.b);
  p.muted = rgb(s.text_muted.r, s.text_muted.g, s.text_muted.b);
  p.hot = rgb(s.magenta.r, s.magenta.g, s.magenta.b);
  p.hero = rgb(s.hero.r, s.hero.g, s.hero.b);
  return p;
}

uint16_t* g_fb = nullptr;

// ---- Settings state (touch + encoder over the shared MenuModel) --------

neon::Config g_cfg;

// Idle display power (docs/SOLAROS_PORTS_HANDOFF.md §3): dims after
// display_dim_s of no touch/encoder input, blanks a stopped transport.
neon::ui::IdleDimmer g_dimmer;
// The input edge that wakes a blanked panel is swallowed — it turns the
// glass back on, it must not also fire the action under the finger.
bool g_swallow_touch = false;
int g_bl_applied = -1;
neon::MenuModel g_menu(&g_cfg);
bool g_settings = false;
int g_scroll = 0;  // first visible row index

void refresh_pal() {
  const neon::ColorTheme theme =
      g_settings ? g_cfg.color_theme : neon_config().color_theme;
  pal = make_pal(theme);
}

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

int iabs(int v) { return v < 0 ? -v : v; }

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
    disc(x, y, 2, on ? pal.neon : pal.ring);
  }
  if (playing) {
    const float a = -static_cast<float>(M_PI) / 2.0f +
                    (2.0f * static_cast<float>(M_PI) * lit) / kDots;
    const int x = kCx + static_cast<int>(kR * std::cos(a));
    const int y = kCy + static_cast<int>(kR * std::sin(a));
    disc(x, y, 5, pal.ink);
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
  bool big_beat = true;    // full-screen beat animation while playing
  uint8_t beat_style = 0;  // neon::BeatStyle as a byte
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
  s.big_beat = cfg.big_beat_display != 0;
  s.beat_style = static_cast<uint8_t>(cfg.beat_style);
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

// Gear + SETTINGS affordance at the top of the dial. Drawn on every live
// view (BPM face and beat stage) so settings is always one tap away.
void draw_settings_affordance() {
  const int label_w = text_w("SETTINGS", 1);
  const int gear_px = 7 * 2;
  const int group_w = gear_px + 6 + label_w;
  const int gx = kCx - group_w / 2;
  draw_gear(gx, 14, 2, pal.muted);
  text(gx + gear_px + 6, 15, "SETTINGS", 1, pal.muted);
}

// Defined in the MIDI section below (used by the settings screen and the
// live faces before their point of definition).
void draw_midi_in_badge();
void send_test_note();

// ---- Beat stage --------------------------------------------------------
//
// The big glanceable beat number (1..quantum) inside the phase ring, shown
// while playing when BEAT (big_beat_display) is on. The pie/pendulum/pulse
// animations were dropped from this build -- the per-pixel pie in particular
// was too costly on the round panel -- so only the number remains. (The
// shared BeatStyle enum still exists for the OLED / web faces.)

void beat_num(const Snap& s) {
  char d[4];
  std::snprintf(d, sizeof(d), "%u", static_cast<unsigned>(s.beat));
  const int scale = 13;
  const uint16_t c = s.beat == 1 ? pal.hot : pal.hero;
  text_cx(kCx, kCy - 7 * scale / 2, d, scale, c);
}

void paint_beat_stage(const Snap& s) {
  refresh_pal();
  fill(0, 0, kW, kH, pal.bg);

  const uint32_t span = s.quantum * 1000u;
  const float progress =
      span != 0 ? static_cast<float>(s.phase % span) / static_cast<float>(span)
                : 0.0f;
  phase_ring(progress, s.playing);
  draw_settings_affordance();

  beat_num(s);
  draw_midi_in_badge();
}

// ---- Live face ---------------------------------------------------------

void paint_live(const Snap& s) {
  // Full-screen beat animation while playing (BEAT + STYLE), matching the
  // OLED's render_home. Otherwise the glanceable hero-BPM dial.
  if (s.playing && s.big_beat) {
    paint_beat_stage(s);
    return;
  }
  refresh_pal();
  fill(0, 0, kW, kH, pal.bg);

  const uint32_t span = s.quantum * 1000u;
  const float progress =
      span != 0 ? static_cast<float>(s.phase % span) / static_cast<float>(span)
                : 0.0f;
  phase_ring(progress, s.playing);

  // Gear + SETTINGS at the top, in place of the old device-name line.
  draw_settings_affordance();

  // Hero tempo. Integer nudges land on whole BPM, so drop the ".0" and
  // render the number larger (scale 6) for a glance read; only a genuinely
  // fractional Link tempo keeps the decimal at the smaller size.
  const bool whole = (s.milli_bpm % 1000u) == 0;
  char big[8];
  const char* bpm_str = s.bpm;
  int bpm_scale = 5;
  if (whole) {
    std::snprintf(big, sizeof(big), "%u",
                  static_cast<unsigned>(s.milli_bpm / 1000u));
    bpm_str = big;
    bpm_scale = 8;
  }
  const int bpm_h = 7 * bpm_scale;
  text_cx(kCx, kCy - bpm_h / 2 - 4, bpm_str, bpm_scale, pal.hero);
  text_cx(kCx, kCy + bpm_h / 2 + 2, "BPM", 1, pal.muted);

  // Peer count under the tempo block.
  char peers[12];
  std::snprintf(peers, sizeof(peers), "%u LINK",
                static_cast<unsigned>(s.peers));
  text_cx(kCx, kCy + bpm_h / 2 + 18, peers, 1, s.peers ? pal.neon : pal.neon_dim);

  // Transport chip near the bottom of the dial.
  const int chip_w = 96;
  const int chip_h = 34;
  const int chip_x = kCx - chip_w / 2;
  const int chip_y = kH - 58;
  fill(chip_x, chip_y, chip_w, chip_h, s.playing ? pal.hot : pal.ring);
  text_cx(kCx, chip_y + (chip_h - 14) / 2, s.playing ? "STOP" : "RUN", 2,
          s.playing ? pal.ink : pal.muted);
  draw_midi_in_badge();
}

// ---- Settings: apply edited config, navigation helpers -----------------

void apply_backlight(int64_t now_us) {
  // Brightness base: the menu's edit copy while settings are open (so a
  // committed BRIGHT edit previews), the live config otherwise (so a web
  // PUT lands without opening the panel).
  const uint8_t base = g_settings ? g_cfg.display_brightness
                                  : neon_config().display_brightness;
  const int eff = g_dimmer.apply(base, now_us);
  if (eff != g_bl_applied) {
    halesp::lcd_gc9a01_backlight_level(static_cast<uint8_t>(eff));
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
  live.color_theme = g_cfg.color_theme;
  live.display_dim_s = g_cfg.display_dim_s;
  live.display_dim_level = g_cfg.display_dim_level;
  live.audio = g_cfg.audio;
  live.audio_follow_enabled = g_cfg.audio_follow_enabled;
  live.audio_follow_phase = g_cfg.audio_follow_phase;
  live.audio_follow_sensitivity = g_cfg.audio_follow_sensitivity;
  live.audio_follow_input = g_cfg.audio_follow_input;
  neon_config_apply(live);
  g_cfg = live;
  apply_backlight(esp_timer_get_time());
}

// ---- Board-aware menu trimming -----------------------------------------
//
// The shared MenuModel exposes every board's settings. The MaTouch has no
// Eurorack jacks and no audio codec, so its panel hides the sections and
// rows that would drive hardware it does not have: the OUTPUTS and AUDIO
// sections, and the pulse/MIDI-timing rows on SYSTEM. MIDI stays (a TRS
// clock jack can be wired to the header — see board_pins.h kPinMidiTx).
// These maps translate a visible row position into the model's real index;
// a null map means "show every row" (identity).

// MENU: LIVE, NETWORK, MIDI, SYSTEM, BACK (drops OUTPUTS[1] and AUDIO[4]).
constexpr int kMenuVis[] = {0, 2, 3, 5, 6};
constexpr int kMenuVisCount = 5;

// MIDI: only CLK OUT drives anything here. BLE is force-disabled on the
// link-sync profile, and CHANNEL / GATE / PITCH CV route MIDI-in to CV
// gates this board has neither the input pin nor the DACs for. The 24 PPQN
// clock on kPinMidiTx is the one live MIDI feature, so it is the one row.
constexpr int kMidiVis[] = {1};
constexpr int kMidiVisCount = 1;

// SYSTEM: QUANTUM, MIDI NDG, SS SYNC, BRIGHT, BEAT, COLOUR, DIM, DIM LVL,
// VERSION, REBOOT.
// MIDI NDG stays because it times the (now live) TRS clock. STYLE is dropped
// with the animations (only the big beat number remains). Also drops
// LATENCY, RESET, SOURCE, IN PPQN, GATE CLK, RST EDGE — each of which times
// a pulse output or an external clock input this board does not have.
constexpr int kSysVis[] = {5, 7, 8, 9, 10, 12, 13, 14, 15, 16};
constexpr int kSysVisCount = 10;

// AUDIO is already omitted from kMenuVis (no codec). If the screen is
// entered anyway, drop FOLLOW / F SENS / F PHASE — no ADC.
constexpr int kAudioVis[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
constexpr int kAudioVisCount = 9;

const int* row_map(neon::MenuModel::Screen scr, int* count) {
  using S = neon::MenuModel::Screen;
  if (scr == S::kMenu) {
    *count = kMenuVisCount;
    return kMenuVis;
  }
  if (scr == S::kMidi) {
    *count = kMidiVisCount;
    return kMidiVis;
  }
  if (scr == S::kAudio) {
    *count = kAudioVisCount;
    return kAudioVis;
  }
  if (scr == S::kSystem) {
    *count = kSysVisCount;
    return kSysVis;
  }
  *count = g_menu.item_count();
  return nullptr;
}

int vis_to_real(const int* map, int vis) { return map != nullptr ? map[vis] : vis; }

int real_to_vis(const int* map, int count, int real) {
  if (map == nullptr) {
    return real;
  }
  for (int i = 0; i < count; ++i) {
    if (map[i] == real) {
      return i;
    }
  }
  return 0;
}

// After navigating to a trimmed screen the model cursor may sit on a hidden
// row (e.g. SYSTEM opens at index 0, which is not visible). Snap it onto
// the first visible row so the highlight and the first click are correct.
void snap_cursor() {
  int count = 0;
  const int* map = row_map(g_menu.screen(), &count);
  if (map == nullptr) {
    return;
  }
  const int cur = g_menu.cursor();
  for (int i = 0; i < count; ++i) {
    if (map[i] == cur) {
      return;
    }
  }
  g_menu.set_cursor(map[0]);
}

void follow_cursor() {
  int count = 0;
  const int* map = row_map(g_menu.screen(), &count);
  const int vis = real_to_vis(map, count, g_menu.cursor());
  if (vis < g_scroll) {
    g_scroll = vis;
  } else if (vis >= g_scroll + kRows) {
    g_scroll = vis - kRows + 1;
  }
  if (g_scroll < 0) {
    g_scroll = 0;
  }
}

// Move the cursor by `detents` across only the visible rows of the current
// screen. On identity-mapped screens (OUTPUTS EDIT, MIDI, CONFIRM) this
// defers to the model so editing/confirm behave unchanged.
void settings_rotate(int detents) {
  int count = 0;
  const int* map = row_map(g_menu.screen(), &count);
  if (map == nullptr) {
    g_menu.on_rotate(detents);
    follow_cursor();
    return;
  }
  if (count <= 0) {
    return;
  }
  int vis = real_to_vis(map, count, g_menu.cursor());
  vis = ((vis + detents) % count + count) % count;
  g_menu.set_cursor(map[vis]);
  follow_cursor();
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
  snap_cursor();
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
  snap_cursor();
  g_scroll = 0;
}

// A row whose value the user edits in place (as opposed to a navigation or
// action row). MENU and NETWORK rows navigate; VERSION is read-only and
// REBOOT is an action.
bool is_value_row(neon::MenuModel::Screen scr, int row) {
  using S = neon::MenuModel::Screen;
  if (scr == S::kMidi) {
    return true;
  }
  if (scr == S::kSystem) {
    return row != neon::MenuModel::kSystemVersionItem &&
           row != neon::MenuModel::kSystemRebootItem;
  }
  return false;  // MENU / NETWORK / OUTPUTS list rows are not value edits
}

// Step a value row by one, in `delta`'s direction. MenuModel clamps its
// numeric ranges (QUANTUM 1..16, BRIGHT 0..255, MIDI NDG ±100 ms), so a
// plain +1 gets stuck at the ceiling — tapping QUANTUM would climb to 16
// and stop. When a step does not move the value it was already at that
// end, so we wrap to the far end by stepping the other way until it stops.
// Enum rows (STYLE, COLOUR) already wrap in the model and take the direct
// path; toggles flip either way for the same reason.
void step_value(int row, int delta) {
  g_menu.set_cursor(row);
  char before[24] = {};
  g_menu.item_value(row, before, sizeof(before));
  g_menu.nudge_value(delta);
  char after[24] = {};
  g_menu.item_value(row, after, sizeof(after));
  if (std::strcmp(before, after) == 0) {
    char prev[24];
    for (;;) {
      std::snprintf(prev, sizeof(prev), "%s", after);
      g_menu.nudge_value(-delta);
      g_menu.item_value(row, after, sizeof(after));
      if (std::strcmp(prev, after) == 0) {
        break;
      }
    }
  }
  commit_menu();
}

// Activate a row on whichever settings screen is showing. `delta` is the
// step direction for value rows (right-tap +1, left-tap or encoder... see
// callers); it is ignored for navigation and action rows.
void settings_activate(int row, int delta = 1) {
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
    g_menu.set_confirm_yes(false);
    g_menu.go_section(S::kConfirm);
    return;
  }
  if (scr == S::kSystem && row == neon::MenuModel::kSystemVersionItem) {
    return;
  }
  step_value(row, delta);
}

void confirm_choice(bool yes) {
  if (yes) {
    ESP_LOGI(kTag, "reboot from settings");
    // Visible confirmation before the panel goes dark, so a fast reboot
    // does not read as "nothing happened".
    refresh_pal();
    fill(0, 0, kW, kH, pal.bg);
    text_cx(kCx, kCy - 8, "REBOOTING", 2, pal.hot);
    halesp::lcd_gc9a01_blit(g_fb, 0, 0, kW, kH);
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
  fill(kCx - w / 2, kBackY0, w, kBackH, pal.surface);
  text_cx(kCx, kBackY0 + (kBackH - 7) / 2, exit_label ? "CLOSE" : "< BACK", 1,
          pal.neon);
}

void paint_network(const Snap& s) {
  const char* mode = s.provisioned ? (s.wifi_up ? "WIFI" : "JOINING")
                                   : "SETUP AP";
  int y = kListY0 + 4;
  text(40, y, "MODE", 1, pal.muted);
  text(150, y, mode, 1, pal.ink);
  y += 30;
  text(40, y, "IP", 1, pal.muted);
  text(150, y, s.ip[0] ? s.ip : "-", 1, pal.ink);
  y += 30;
  char pc[12];
  std::snprintf(pc, sizeof(pc), "%u", static_cast<unsigned>(s.peers));
  text(40, y, "PEERS", 1, pal.muted);
  text(150, y, pc, 1, pal.ink);
  // Tappable "scan & join" chip (also fired by an encoder click here).
  const int cw = 150;
  fill(kCx - cw / 2, kBackY0 - 40, cw, 30, pal.surface2);
  text_cx(kCx, kBackY0 - 40 + (30 - 7) / 2, "SCAN & JOIN", 1, pal.neon);
}

constexpr int kNetScanY0 = kBackY0 - 40;
constexpr int kNetScanH = 30;

void paint_confirm() {
  text_cx(kCx, kListY0 + 6, "REBOOT?", 2, pal.ink);
  text_cx(kCx, kListY0 + 34, "clock stops", 1, pal.muted);
  const int bw = 70;
  const int bh = 40;
  const int by = kCy + 20;
  const bool yes = g_menu.confirm_yes();
  const int no_x = kCx - 8 - bw;
  const int yes_x = kCx + 8;
  fill(no_x, by, bw, bh, pal.surface);
  text_cx(no_x + bw / 2, by + (bh - 14) / 2, "NO", 2, pal.ink);
  fill(yes_x, by, bw, bh, pal.hot);
  text_cx(yes_x + bw / 2, by + (bh - 14) / 2, "YES", 2, pal.ink);
  // Selection ring so the encoder path shows which choice is armed (touch
  // hits either side directly). Twisting toggles it; a click commits it.
  const int sx = yes ? yes_x : no_x;
  fill(sx - 3, by - 3, bw + 6, 3, pal.ink);
  fill(sx - 3, by + bh, bw + 6, 3, pal.ink);
  fill(sx - 3, by - 3, 3, bh + 6, pal.ink);
  fill(sx + bw, by - 3, 3, bh + 6, pal.ink);
  text_cx(kCx, kH - 34, yes ? "click = REBOOT" : "twist to YES", 1,
          pal.neon_dim);
}

void paint_settings(const Snap& s) {
  using S = neon::MenuModel::Screen;
  refresh_pal();
  fill(0, 0, kW, kH, pal.bg);

  const S scr = g_menu.screen();
  text_cx(kCx, 12, g_menu.screen_title(), 2, pal.neon);
  fill(kCx - 70, 32, 140, 2, pal.neon_dim);

  if (scr == S::kConfirm) {
    paint_confirm();
    return;
  }
  if (scr == S::kNetwork) {
    paint_network(s);
    paint_back_chip(false);
    return;
  }

  int n = 0;
  const int* map = row_map(scr, &n);
  follow_cursor();
  for (int i = 0; i < kRows; ++i) {
    const int vis = g_scroll + i;
    if (vis >= n) {
      break;
    }
    const int idx = vis_to_real(map, vis);
    const int y = kListY0 + i * kRowH;
    const bool cur = idx == g_menu.cursor();
    if (cur) {
      fill(28, y, kW - 56, kRowH - 4, pal.surface2);
    }
    text(38, y + (kRowH - 4 - 7) / 2, g_menu.item_label(idx), 1,
         cur ? pal.ink : pal.muted);
    char val[40] = {};
    if (scr == S::kSystem && idx == neon::MenuModel::kSystemVersionItem) {
      std::snprintf(val, sizeof(val), "%s", s.firmware);
    } else {
      g_menu.item_value(idx, val, sizeof(val));
    }
    text(202 - text_w(val, 1), y + (kRowH - 4 - 7) / 2, val, 1, pal.neon);
  }

  // Scroll hints when the list runs past the window.
  if (g_scroll > 0) {
    text_cx(kCx, kListY0 - 10, "\x18", 1, pal.neon_dim);  // up
  }
  if (g_scroll + kRows < n) {
    text_cx(kCx, kBackY0 - 12, "\x19", 1, pal.neon_dim);  // down
  }

  // A "send a note" chip on the MIDI screen (mirrors the Network scan chip):
  // an audible check that the synth on the OUT jack is hearing us.
  if (scr == S::kMidi) {
    const int cw = 150;
    fill(kCx - cw / 2, kBackY0 - 40, cw, 30, pal.surface2);
    text_cx(kCx, kBackY0 - 40 + (30 - 7) / 2, "TEST NOTE", 1, pal.neon);
  }

  paint_back_chip(scr == S::kMenu);
}

// ---- WiFi join flow ----------------------------------------------------

void wifi_connect() {
  // Ignore a premature commit: WPA2 needs at least 8 characters, so a
  // half-typed password should not kick off a connect. (Combined with the
  // RAM-only apply below, a wrong password can no longer reach flash.)
  if (!g_join_open && g_pass_len < 8) {
    return;
  }
  neon::Config cfg = neon_config();
  std::snprintf(cfg.wifi[0].ssid, sizeof(cfg.wifi[0].ssid), "%s", g_join_ssid);
  std::snprintf(cfg.wifi[0].pass, sizeof(cfg.wifi[0].pass), "%s",
                g_join_open ? "" : g_pass);
  cfg.wifi[0].hidden = 0;
  // Try the credential without persisting it. Only wifi_tick(), once an IP
  // actually arrives, writes it to NVS -- so a failed attempt leaves flash
  // untouched instead of poisoning it with a password that never connects.
  neon_config_apply_ram(cfg);
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
  fill(0, 0, kW, kH, pal.bg);
  text_cx(kCx, kCy - 8, "SCANNING", 2, pal.neon);
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
  fill(0, 0, kW, kH, pal.bg);
  text_cx(kCx, 12, "WIFI", 2, pal.neon);
  fill(kCx - 70, 32, 140, 2, pal.neon_dim);

  if (g_ap_count == 0) {
    text_cx(kCx, kCy - 8, "NO NETWORKS", 1, pal.muted);
    text_cx(kCx, kCy + 8, "click to rescan", 1, pal.neon_dim);
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
      fill(28, y, kW - 56, kRowH - 4, pal.surface2);
    }
    char ssid[24];
    clip_str(ssid, sizeof(ssid), g_aps[idx].ssid, 22);
    text(38, y + (kRowH - 4 - 7) / 2, ssid, 1, cur ? pal.ink : pal.muted);
    // Lock glyph for secured networks.
    if (g_aps[idx].open == 0) {
      text(190, y + (kRowH - 4 - 7) / 2, "\x07", 1, pal.neon);
    }
  }
  if (g_ap_scroll > 0) {
    text_cx(kCx, kListY0 - 10, "\x18", 1, pal.neon_dim);
  }
  if (g_ap_scroll + kRows < g_ap_count) {
    text_cx(kCx, kBackY0 - 12, "\x19", 1, pal.neon_dim);
  }
  paint_back_chip(false);
}

void paint_wifi_pass() {
  fill(0, 0, kW, kH, pal.bg);
  char title[26];
  clip_str(title, sizeof(title), g_join_ssid, 24);
  text_cx(kCx, 14, title, 1, pal.muted);

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
  text_cx(kCx, 46, g_pass_len ? line : "_", 2, pal.ink);

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
    const uint16_t col = off == 0 ? pal.neon : pal.neon_dim;
    const int x = kCx + off * 34;
    text(x - text_w(buf, scale) / 2, cy - 7 * scale / 2, buf, scale, col);
  }

  text_cx(kCx, kH - 40, "turn pick . click add", 1, pal.neon_dim);
  text_cx(kCx, kH - 26, "hold = back", 1, pal.neon_dim);
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
    // Proven good (we have an IP): now it is safe to persist. This is the
    // only path that writes the joined credential to flash.
    neon_config_save(neon_config());
    ESP_LOGI(kTag, "joined; credentials saved");
  }
  if (esp_timer_get_time() - g_connected_us > 1200000) {
    close_settings();  // g_settings=false, g_wifi=kOff → live home screen
    return true;
  }
  return false;
}

void paint_wifi_connecting() {
  fill(0, 0, kW, kH, pal.bg);
  const bool up = neon_wifi_sta_got_ip();
  const int64_t dt = esp_timer_get_time() - g_connect_us;
  if (up) {
    text_cx(kCx, kCy - 20, "CONNECTED", 2, pal.neon);
    text_cx(kCx, kCy + 8, g_join_ssid, 1, pal.ink);
  } else if (dt > 20000000) {
    text_cx(kCx, kCy - 20, "NO CONNECT", 2, pal.hot);
    text_cx(kCx, kCy + 8, "check password", 1, pal.muted);
    text_cx(kCx, kH - 40, "hold = back", 1, pal.neon_dim);
  } else {
    text_cx(kCx, kCy - 20, "CONNECTING", 2, pal.neon);
    const int dots = static_cast<int>((dt / 400000) % 4);
    char d[5] = "....";
    d[dots] = '\0';
    text_cx(kCx, kCy + 8, d, 2, pal.ink);
  }
}

void paint_wifi() {
  refresh_pal();
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
    // Whole lower half splits left = NO, right = YES: no centre dead zone,
    // and each target is half the dial rather than a 70 px pill.
    const int by = kCy + 20;
    if (y >= by - 24) {
      confirm_choice(x >= kCx);
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

  if (scr == S::kMidi && in_rect(x, y, kCx - 75, kBackY0 - 40, 150, 30)) {
    send_test_note();
    return;
  }

  // A row in the list window. On value rows the tap's side sets direction:
  // left half decrements, right half increments, so a bounded value moves
  // both ways (QUANTUM no longer sticks at 16). Navigation rows ignore it.
  if (y >= kListY0 && y < kListY0 + kRows * kRowH) {
    const int i = (y - kListY0) / kRowH;
    const int vis = g_scroll + i;
    int count = 0;
    const int* map = row_map(scr, &count);
    if (vis >= 0 && vis < count) {
      const int idx = vis_to_real(map, vis);
      const int delta = (is_value_row(scr, idx) && x < kCx) ? -1 : 1;
      settings_activate(idx, delta);
    }
  }
}

// ---- MIDI test tone + external-clock follow ----------------------------

// A one-shot note to whatever synth is on the OUT jack (the M5 unit's
// SAM2695 hears it in either switch position, since RXD always feeds the
// synth). Proves the MaTouch -> unit TX path audibly, independent of the
// DIN wiring and the KeyStep. Uses the buffered driver write; safe because
// this is a stopped-state test, so the ISR clock path is not also writing.
int64_t g_test_off_us = 0;

void send_test_note() {
  const uint8_t on[3] = {0x90, 60, 100};  // ch 1 note-on, middle C
  halesp::midi_uart_send(on, sizeof(on));
  g_test_off_us = esp_timer_get_time() + 350000;
}

void test_note_tick() {
  if (g_test_off_us != 0 && esp_timer_get_time() >= g_test_off_us) {
    const uint8_t off[3] = {0x80, 60, 0};
    halesp::midi_uart_send(off, sizeof(off));
    g_test_off_us = 0;
  }
}

// External MIDI clock follow. Bytes arrive on RX (GPIO44) from a MIDI IN
// source; 24 PPQN clock is counted over a ~1 s window to estimate BPM
// (pushed to the Link session as kSetTempo), and Start/Stop drive the
// transport. Tempo + transport follow, not sample-accurate phase lock.
int g_midi_clocks = 0;
int64_t g_midi_window_us = 0;
int64_t g_midi_last_rx_us = 0;

void midi_in_tick() {
  const int64_t now = esp_timer_get_time();
  uint8_t buf[128];
  for (;;) {
    const int n = halesp::midi_uart_read(buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    for (int i = 0; i < n; ++i) {
      const uint8_t b = buf[i];
      if (b == 0xF8) {  // clock
        ++g_midi_clocks;
        g_midi_last_rx_us = now;
      } else if (b == 0xFA || b == 0xFB) {  // start / continue
        ControlCommand c{};
        c.kind = ControlCommand::Kind::kPlayNow;
        control_queue_push(c);
        g_midi_last_rx_us = now;
      } else if (b == 0xFC) {  // stop
        ControlCommand c{};
        c.kind = ControlCommand::Kind::kStopNow;
        control_queue_push(c);
        g_midi_last_rx_us = now;
      }
    }
    if (n < static_cast<int>(sizeof(buf))) {
      break;
    }
  }
  if (g_midi_window_us == 0) {
    g_midi_window_us = now;
  }
  const int64_t dt = now - g_midi_window_us;
  if (dt >= 1000000) {
    if (g_midi_clocks > 0) {
      // 24 PPQN: milli_bpm = clocks / (dt seconds) / 24 * 60 * 1000
      //                    = clocks * 2.5e9 / dt_us.
      const uint32_t milli_bpm = static_cast<uint32_t>(
          (static_cast<int64_t>(g_midi_clocks) * 2500000000LL) / dt);
      if (milli_bpm >= neon::kMinMilliBpm && milli_bpm <= neon::kMaxMilliBpm) {
        ControlCommand c{};
        c.kind = ControlCommand::Kind::kSetTempo;
        c.arg = static_cast<int32_t>(milli_bpm);
        control_queue_push(c);
      }
    }
    g_midi_clocks = 0;
    g_midi_window_us = now;
  }
}

// True for ~0.6 s after the last realtime byte, for the live-face badge.
bool midi_in_active() {
  return g_midi_last_rx_us != 0 &&
         (esp_timer_get_time() - g_midi_last_rx_us) < 600000;
}

void draw_midi_in_badge() {
  if (midi_in_active()) {
    text_cx(kCx, kH - 18, "MIDI IN", 1, pal.neon);
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

// True when the current settings list is taller than the window, so a
// touch drag has somewhere to go.
bool list_scrollable() {
  using S = neon::MenuModel::Screen;
  const S scr = g_menu.screen();
  if (scr == S::kConfirm || scr == S::kNetwork || scr == S::kHome) {
    return false;
  }
  int count = 0;
  row_map(scr, &count);
  return count > kRows;
}

// Single-finger touch: a press that lifts without much travel is a tap
// (fired on release, at the press point); vertical travel on a scrollable
// settings list scrolls it instead and suppresses the tap. Dragging the
// list also carries the cursor with it so the encoder's follow-cursor does
// not snap the window back on the next frame.
void handle_touch() {
  static bool was_down = false;
  static int start_x = 0;
  static int start_y = 0;
  static int start_scroll = 0;
  static bool dragging = false;
  static bool on_list = false;

  int x = 0;
  int y = 0;
  const bool down = halesp::cst816_poll(&x, &y);
  if (down) {
    g_dimmer.note_activity(esp_timer_get_time());
  }
  if (g_swallow_touch) {
    // This gesture only woke the blanked panel; drop it whole.
    if (!down) {
      g_swallow_touch = false;
    }
    was_down = down;
    return;
  }

  if (down && !was_down) {
    start_x = x;
    start_y = y;
    start_scroll = g_scroll;
    dragging = false;
    on_list = g_settings && g_wifi == Wifi::kOff && list_scrollable();
  } else if (down && was_down) {
    if (on_list) {
      if (iabs(y - start_y) > 8) {
        dragging = true;
      }
      if (dragging) {
        int count = 0;
        const int* map = row_map(g_menu.screen(), &count);
        const int max_scroll = count > kRows ? count - kRows : 0;
        int ns = start_scroll + (start_y - y) / kRowH;
        if (ns < 0) {
          ns = 0;
        }
        if (ns > max_scroll) {
          ns = max_scroll;
        }
        g_scroll = ns;
        g_menu.set_cursor(vis_to_real(map, ns));  // keep follow_cursor happy
      }
    }
  } else if (!down && was_down && !dragging) {
    // Release without a drag → a tap at the press point.
    if (g_settings && g_wifi != Wifi::kOff) {
      wifi_tap(start_x, start_y);
    } else if (g_settings) {
      settings_tap(start_x, start_y);
    } else if (in_rect(start_x, start_y, kGearZoneX0, kGearZoneY0,
                       kGearZoneX1 - kGearZoneX0,
                       kGearZoneY1 - kGearZoneY0)) {
      open_settings();
      ESP_LOGI(kTag, "touch: open settings");
    }
  }
  was_down = down;
}

void handle_encoder() {
  // Encoder A/B read CCW-positive on this board; negate so CW is "forward".
  const int detents = -halesp::encoder_take_detents();
  const halesp::EncoderPress press = halesp::encoder_take_press();
  if (detents != 0 || press != halesp::EncoderPress::kNone) {
    g_dimmer.note_activity(esp_timer_get_time());
  }

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
    settings_rotate(detents);
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
  refresh_pal();

  // Splash so the panel proves itself before Link is up.
  fill(0, 0, kW, kH, pal.bg);
  phase_ring(0.0f, false);
  text_cx(kCx, kCy - 20, "NEON", 3, pal.neon);
  text_cx(kCx, kCy + 12, "link-mat", 1, pal.muted);
  halesp::lcd_gc9a01_blit(g_fb, 0, 0, kW, kH);
  g_dimmer.note_activity(esp_timer_get_time());
  apply_backlight(esp_timer_get_time());
  vTaskDelay(pdMS_TO_TICKS(700));

  for (;;) {
    const int64_t now = esp_timer_get_time();
    {
      const neon::Config& live = neon_config();
      g_dimmer.configure(live.display_dim_s, live.display_dim_level);
    }
    if (g_dimmer.level(now) == neon::ui::IdleDimmer::Level::kBlank) {
      // Panel dark: drain inputs without acting on them — the edge that
      // wakes the glass must not also nudge tempo or tap a control —
      // and skip the 115 KB SPI blit. MIDI follow keeps running.
      int tx = 0;
      int ty = 0;
      const bool tdown = halesp::cst816_poll(&tx, &ty);
      const int det = halesp::encoder_take_detents();
      const halesp::EncoderPress pr = halesp::encoder_take_press();
      if (tdown || det != 0 || pr != halesp::EncoderPress::kNone) {
        g_dimmer.note_activity(now);
        g_swallow_touch = tdown;  // drop the rest of the waking gesture
      }
      midi_in_tick();
      test_note_tick();
      wifi_tick();
      // A transport started remotely must un-blank: playing never
      // blanks, so the dimmer falls back to kDim and painting resumes.
      neon::TimelineSnapshot tl{};
      timeline_bus().read(tl);
      g_dimmer.set_playing(tl.playing != 0);
      apply_backlight(now);
      vTaskDelay(pdMS_TO_TICKS(g_dimmer.frame_interval_hint_ms(now)));
      continue;
    }
    handle_encoder();
    handle_touch();
    midi_in_tick();    // follow external MIDI clock + start/stop on RX
    test_note_tick();  // release a pending test note

    wifi_tick();  // auto-save + return home once the join gets an IP

    const Snap s = snapshot();
    g_dimmer.set_playing(s.playing);
    apply_backlight(now);
    const int hint = g_dimmer.frame_interval_hint_ms(now);
    if (g_settings && g_wifi != Wifi::kOff) {
      paint_wifi();
    } else if (g_settings) {
      paint_settings(s);
    } else {
      paint_live(s);
    }
    halesp::lcd_gc9a01_blit(g_fb, 0, 0, kW, kH);
    vTaskDelay(pdMS_TO_TICKS(40 + hint));  // ~25 fps active, relaxed dim
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
