// RP2040 UART MIDI ↔ USB MIDI 1.0 (Adafruit TinyUSB).
// CrowPanel IO21 (3.3 V UART @ 31250) → GP1. USB enumerates as
// "link-sync MIDI".
//
// XIAO RP2040 NeoPixel (GP12, power GP11):
//   red    USB not mounted
//   blue   mounted, stopped
//   green  playing, flash white on the quarter-note (every 24 clocks)

#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

#ifdef PIN_NEOPIXEL
#include <Adafruit_NeoPixel.h>
#endif

#ifndef MIDI_RX_PIN
#define MIDI_RX_PIN 1
#endif
#ifndef MIDI_TX_PIN
#define MIDI_TX_PIN 0
#endif

namespace {

constexpr uint32_t kUartBaud = 31250;
constexpr uint8_t kClocksPerBeat = 24;
constexpr uint32_t kBeatFlashMs = 55;
constexpr uint32_t kClockHoldMs = 250;

Adafruit_USBD_MIDI g_midi;

#ifdef PIN_NEOPIXEL
Adafruit_NeoPixel g_px(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
#endif

bool g_playing = false;
uint8_t g_clocks = 0;
uint32_t g_beat_until_ms = 0;
uint32_t g_last_clock_ms = 0;
uint32_t g_shown = 0xFFFFFFFFu;

uint32_t pack(uint8_t r, uint8_t g, uint8_t b) {
  return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void pixel(uint8_t r, uint8_t g, uint8_t b) {
#ifdef PIN_NEOPIXEL
  const uint32_t c = pack(r, g, b);
  if (c == g_shown) {
    return;
  }
  g_shown = c;
  g_px.setPixelColor(0, g_px.Color(r, g, b));
  g_px.show();
#else
  (void)r;
  (void)g;
  (void)b;
#endif
}

void paint() {
  if (!TinyUSBDevice.mounted()) {
    pixel(18, 0, 0);
    return;
  }
  if (g_beat_until_ms != 0 &&
      static_cast<int32_t>(millis() - g_beat_until_ms) < 0) {
    pixel(40, 40, 40);
    return;
  }
  g_beat_until_ms = 0;
  if (g_playing) {
    pixel(0, 22, 4);
  } else {
    pixel(0, 4, 28);
  }
}

void on_midi_byte(uint8_t b) {
  switch (b) {
    case 0xFA:  // Start
    case 0xFB:  // Continue
      g_playing = true;
      g_clocks = 0;
      break;
    case 0xFC:  // Stop
      g_playing = false;
      g_clocks = 0;
      g_beat_until_ms = 0;
      break;
    case 0xF8: {  // Clock — 24 PPQN
      g_last_clock_ms = millis();
      if (!g_playing) {
        g_playing = true;  // joined mid-bar
      }
      if (++g_clocks >= kClocksPerBeat) {
        g_clocks = 0;
        g_beat_until_ms = millis() + kBeatFlashMs;
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace

void setup() {
#ifdef PIN_NEOPIXEL
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
    pixel(18, 0, 0);
  }
  pixel(0, 4, 28);
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
  }

  paint();
}
