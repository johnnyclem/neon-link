// RP2040 UART MIDI ↔ USB MIDI 1.0 (Adafruit TinyUSB).
// CrowPanel IO21 (3.3 V UART @ 31250) → GP1 / XIAO D7.
//
// XIAO RP2040 onboard NeoPixel (GP12 / power GP11), unless MATRIX_PIN:
//   red    USB not mounted
//   blue   mounted, stopped
//   green  playing, flash white on the quarter-note
//
// Seeed 6x10 RGB MATRIX (60 WS2812 on D0): giant 1–4, neon-link style.

#include <Adafruit_NeoPixel.h>
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

#ifndef MIDI_RX_PIN
#define MIDI_RX_PIN 1
#endif
#ifndef MIDI_TX_PIN
#define MIDI_TX_PIN 0
#endif

#ifdef MATRIX_PIN
// Seeed 6×10 PCB is 6 columns × 10 rows, row-major L→R then down (not
// serpentine). Wiki "10 columns" is wrong; Eagle chain is 6-wide.
#ifndef MATRIX_W
#define MATRIX_W 6
#endif
#ifndef MATRIX_H
#define MATRIX_H 10
#endif
#ifndef MATRIX_COUNT
#define MATRIX_COUNT (MATRIX_W * MATRIX_H)
#endif
#endif

namespace {

constexpr uint32_t kUartBaud = 31250;
constexpr uint8_t kClocksPerBeat = 24;
constexpr uint8_t kBeatsPerBar = 4;
constexpr uint32_t kBeatFlashMs = 70;
constexpr uint32_t kClockHoldMs = 250;

Adafruit_USBD_MIDI g_midi;

#ifdef MATRIX_PIN
Adafruit_NeoPixel g_px(MATRIX_COUNT, MATRIX_PIN, NEO_GRB + NEO_KHZ800);
#elif defined(PIN_NEOPIXEL)
Adafruit_NeoPixel g_px(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
#endif

bool g_playing = false;
uint8_t g_clocks = 0;
uint8_t g_beat = 1;  // 1..4
uint32_t g_beat_until_ms = 0;
uint32_t g_last_clock_ms = 0;
uint32_t g_shown = 0xFFFFFFFFu;
bool g_dirty = true;

#ifdef MATRIX_PIN
// 6-bit rows, bit 5 = left (x=0). Tall 1–4 for the 6×10 portrait grid.
const uint16_t kDigit[4][10] = {
    {0b001100, 0b011100, 0b001100, 0b001100, 0b001100, 0b001100, 0b001100,
     0b001100, 0b011110, 0b011110},
    {0b011110, 0b110011, 0b000011, 0b000011, 0b000110, 0b001100, 0b011000,
     0b110000, 0b110000, 0b111111},
    {0b011110, 0b110011, 0b000011, 0b000011, 0b001110, 0b000011, 0b000011,
     0b000011, 0b110011, 0b011110},
    {0b110011, 0b110011, 0b110011, 0b110011, 0b111111, 0b000011, 0b000011,
     0b000011, 0b000011, 0b000011},
};

int pix(int x, int y) {
#if MATRIX_FLIP_X
  x = MATRIX_W - 1 - x;
#endif
#if MATRIX_FLIP_Y
  y = MATRIX_H - 1 - y;
#endif
#if MATRIX_SERPENTINE
  if (y & 1) {
    x = MATRIX_W - 1 - x;
  }
#endif
  return y * MATRIX_W + x;
}

void matrix_digit(uint8_t digit, uint8_t r, uint8_t g, uint8_t b) {
  g_px.clear();
  if (digit < 1 || digit > 4) {
    g_px.show();
    return;
  }
  const uint16_t* rows = kDigit[digit - 1];
  const uint32_t c = g_px.Color(r, g, b);
  for (int y = 0; y < MATRIX_H; ++y) {
    const uint16_t bits = rows[y];
    for (int x = 0; x < MATRIX_W; ++x) {
      if (bits & (1u << (MATRIX_W - 1 - x))) {
        g_px.setPixelColor(pix(x, y), c);
      }
    }
  }
  g_px.show();
}

void matrix_fill(uint8_t r, uint8_t g, uint8_t b) {
  g_px.fill(g_px.Color(r, g, b));
  g_px.show();
}
#endif

void paint() {
#ifdef MATRIX_PIN
  const bool mounted = TinyUSBDevice.mounted();
  const bool flash = g_beat_until_ms != 0 &&
                     static_cast<int32_t>(millis() - g_beat_until_ms) < 0;
  if (!flash) {
    g_beat_until_ms = 0;
  }
  const uint32_t key = (mounted ? 1u : 0) | (g_playing ? 2u : 0) |
                       (uint32_t(g_beat) << 8) | (flash ? 0x10000u : 0);
  if (key == g_shown && !g_dirty) {
    return;
  }
  g_shown = key;
  g_dirty = false;
  if (!mounted) {
    matrix_fill(12, 0, 0);
    return;
  }
  if (!g_playing) {
    matrix_digit(g_beat, 0, 2, 18);
    return;
  }
  if (flash) {
    matrix_digit(g_beat, 40, 40, 40);
    return;
  }
  if (g_beat == 1) {
    matrix_digit(1, 28, 10, 0);  // downbeat warmer
  } else {
    matrix_digit(g_beat, 0, 22, 6);
  }
#elif defined(PIN_NEOPIXEL)
  uint8_t r = 0, g = 0, b = 0;
  if (!TinyUSBDevice.mounted()) {
    r = 18;
  } else if (g_beat_until_ms != 0 &&
             static_cast<int32_t>(millis() - g_beat_until_ms) < 0) {
    r = g = b = 40;
  } else {
    g_beat_until_ms = 0;
    if (g_playing) {
      g = 22;
      b = 4;
    } else {
      g = 4;
      b = 28;
    }
  }
  const uint32_t c = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
  if (c == g_shown) {
    return;
  }
  g_shown = c;
  g_px.setPixelColor(0, g_px.Color(r, g, b));
  g_px.show();
#else
  (void)g_dirty;
#endif
}

void on_midi_byte(uint8_t b) {
  switch (b) {
    case 0xFA:
    case 0xFB:
      g_playing = true;
      g_clocks = 0;
      g_beat = 1;
      g_beat_until_ms = millis() + kBeatFlashMs;
      g_dirty = true;
      break;
    case 0xFC:
      g_playing = false;
      g_clocks = 0;
      g_beat_until_ms = 0;
      g_dirty = true;
      break;
    case 0xF8:
      g_last_clock_ms = millis();
      if (!g_playing) {
        g_playing = true;
        g_dirty = true;
      }
      if (++g_clocks >= kClocksPerBeat) {
        g_clocks = 0;
        g_beat = static_cast<uint8_t>(g_beat % kBeatsPerBar + 1);
        g_beat_until_ms = millis() + kBeatFlashMs;
        g_dirty = true;
      }
      break;
    default:
      break;
  }
}

}  // namespace

void setup() {
#ifdef MATRIX_PIN
  g_px.begin();
  g_px.setBrightness(36);
  g_px.clear();
  g_px.show();
#elif defined(PIN_NEOPIXEL)
#ifdef NEOPIXEL_POWER
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
#endif
  g_px.begin();
  g_px.setBrightness(40);
  g_px.clear();
  g_px.show();
#endif

  TinyUSBDevice.setID(0x1209, 0x81C1);
  TinyUSBDevice.setManufacturerDescriptor("neon-link");
  TinyUSBDevice.setProductDescriptor("link-sync MIDI");
  g_midi.setCableName(1, "link-sync");
  g_midi.begin();

  Serial1.setRX(MIDI_RX_PIN);
  Serial1.setTX(MIDI_TX_PIN);
  Serial1.begin(kUartBaud);

  while (!TinyUSBDevice.mounted() && millis() < 3000) {
    delay(10);
    g_dirty = true;
    paint();
  }
  g_dirty = true;
  paint();
}

void loop() {
  while (Serial1.available() > 0) {
    const uint8_t b = static_cast<uint8_t>(Serial1.read());
    on_midi_byte(b);
    g_midi.write(b);
  }
  while (g_midi.available() > 0) {
    const uint8_t b = static_cast<uint8_t>(g_midi.read());
    on_midi_byte(b);
    Serial1.write(b);
  }

  if (g_playing && g_last_clock_ms != 0 &&
      (millis() - g_last_clock_ms) > kClockHoldMs) {
    g_playing = false;
    g_clocks = 0;
    g_beat_until_ms = 0;
    g_dirty = true;
  }

  paint();
}
