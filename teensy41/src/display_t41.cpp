#include "display_t41.h"

#include <Arduino.h>
#include <ILI9341_t3.h>

#include <cstring>

#include "board_pins_t41.h"

namespace {

// design/tokens.json, reduced to RGB565.
constexpr uint16_t rgb565(uint32_t rgb) {
  return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) |
                               ((rgb >> 3) & 0x001F));
}
constexpr uint16_t kColBg = rgb565(0x0B0C0F);      // color.bg
constexpr uint16_t kColNeon = rgb565(0x00F0FF);    // color.neon
constexpr uint16_t kColBorder = rgb565(0x2A2E38);  // color.border
constexpr uint16_t kColMuted = rgb565(0x8B909A);   // color.text-muted
constexpr uint16_t kColGreen = rgb565(0x3DFF9A);   // color.success

ILI9341_t3 g_tft(kPinTftCs, kPinTftDc, kPinTftRst, 11, 13, 12);

// Full UI zone as RGB565, composed then pushed in one writeRect. 112.5 KB
// — lives in RAM2 (DMAMEM) to keep RAM1 free for code and stacks.
DMAMEM uint16_t g_frame[DisplayT41::kUiSize * DisplayT41::kUiSize];

// 240 -> 128 nearest-neighbour source column/row per target pixel.
uint8_t g_map[DisplayT41::kUiSize];

// Last pushed UI framebuffer, for change detection.
uint8_t g_last_fb[neon::Framebuffer::kSize];
bool g_have_last = false;

const char* const kBtnLabels[kBtnCount] = {"+", "-", "OK", "BACK"};

}  // namespace

void DisplayT41::button_rect(int index, int* x, int* y, int* w, int* h) {
  constexpr int kPad = 4;
  const int slot = (240 - 5 * kPad) / kBtnCount;  // 56
  *x = kStripX + kPad;
  *y = kPad + index * (slot + kPad);
  *w = kStripW - 2 * kPad;
  *h = slot;
}

void DisplayT41::init() {
  for (int i = 0; i < kUiSize; ++i) {
    g_map[i] = static_cast<uint8_t>((i * neon::Framebuffer::kWidth) / kUiSize);
  }
  g_tft.begin();
  g_tft.setRotation(1);  // 320 wide, 240 tall
  g_tft.fillScreen(kColBg);
  if (kPinTftBacklight >= 0) {
    pinMode(kPinTftBacklight, OUTPUT);
    analogWrite(kPinTftBacklight, 255);
  }
  g_have_last = false;
}

void DisplayT41::draw_ui(const neon::Framebuffer& fb, bool force) {
  if (!force && g_have_last &&
      std::memcmp(g_last_fb, fb.data(), sizeof(g_last_fb)) == 0) {
    return;
  }
  std::memcpy(g_last_fb, fb.data(), sizeof(g_last_fb));
  g_have_last = true;

  const uint8_t* data = fb.data();
  uint16_t* out = g_frame;
  for (int y = 0; y < kUiSize; ++y) {
    const int sy = g_map[y];
    // SSD1306 page layout: byte holds 8 vertical pixels.
    const uint8_t* row = data + (sy >> 3) * neon::Framebuffer::kWidth;
    const uint8_t bit = static_cast<uint8_t>(sy & 7);
    for (int x = 0; x < kUiSize; ++x) {
      *out++ = ((row[g_map[x]] >> bit) & 1u) ? kColNeon : kColBg;
    }
  }
  g_tft.writeRect(0, 0, kUiSize, kUiSize, g_frame);
}

void DisplayT41::draw_strip(uint8_t pressed_mask, bool playing, bool force) {
  static uint8_t last_mask = 0xFF;
  static int last_playing = -1;
  if (!force && pressed_mask == last_mask &&
      static_cast<int>(playing) == last_playing) {
    return;
  }
  const bool full = force || last_playing < 0;
  if (full) {
    g_tft.fillRect(kStripX, 0, kStripW, 240, kColBg);
    g_tft.drawFastVLine(kStripX, 0, 240, kColBorder);
  }

  g_tft.setTextSize(2);
  for (int i = 0; i < kBtnCount; ++i) {
    const bool pressed = (pressed_mask >> i) & 1u;
    const bool was = (last_mask >> i) & 1u;
    if (!full && pressed == was) {
      continue;
    }
    int x, y, w, h;
    button_rect(i, &x, &y, &w, &h);
    // BACK doubles as the transport tell-tale: green frame while playing.
    const uint16_t frame =
        (i == kBtnBack && playing) ? kColGreen : kColMuted;
    g_tft.fillRect(x, y, w, h, pressed ? kColNeon : kColBg);
    g_tft.drawRect(x, y, w, h, pressed ? kColNeon : frame);
    const int tw = static_cast<int>(strlen(kBtnLabels[i])) * 12;
    g_tft.setCursor(x + (w - tw) / 2, y + (h - 16) / 2);
    g_tft.setTextColor(pressed ? kColBg : kColNeon);
    g_tft.print(kBtnLabels[i]);
  }
  last_mask = pressed_mask;
  last_playing = playing;
}

void DisplayT41::set_backlight(uint8_t level) {
  if (kPinTftBacklight >= 0) {
    analogWrite(kPinTftBacklight, level);
  }
}
