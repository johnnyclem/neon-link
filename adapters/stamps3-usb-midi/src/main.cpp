// Stamp S3 UART MIDI ↔ USB MIDI 1.0 (TinyUSB).
// CrowPanel IO21 (3.3 V UART @ 31250) → G1. USB-C enumerates as
// "link-sync MIDI". Follows Espressif's MidiInterface example.

#if ARDUINO_USB_MODE
#error This firmware needs USB-OTG (TinyUSB). Build with ARDUINO_USB_MODE=0.
#endif

#include "USB.h"
#include "USBMIDI.h"

#include <Arduino.h>

namespace {

constexpr int kPinRx = 1;    // CrowPanel IO21 → here
constexpr int kPinTx = 3;    // optional MIDI OUT
constexpr int kPinRgb = 21;  // onboard WS2812
constexpr int kPinBtn = 0;
constexpr uint32_t kUartBaud = 31250;

USBMIDI g_midi;
uint32_t g_led_until_ms = 0;

void rgb(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(kPinRgb, r, g, b); }

void led_kick() {
  rgb(0, 40, 0);
  g_led_until_ms = millis() + 12;
}

void led_poll() {
  if (g_led_until_ms != 0 &&
      static_cast<int32_t>(millis() - g_led_until_ms) >= 0) {
    rgb(0, 0, 8);
    g_led_until_ms = 0;
  }
}

// USB-MIDI CIN → how many of byte1..byte3 are serial MIDI.
constexpr int8_t kCinBytes[16] = {-1, -1, 2, 3, 3, 1, 2, 3,
                                  3,  3,  3, 3, 2, 2, 3, 1};

}  // namespace

void setup() {
  pinMode(kPinBtn, INPUT_PULLUP);
  rgb(0, 0, 0);

  Serial.begin(115200);
  Serial1.begin(kUartBaud, SERIAL_8N1, kPinRx, kPinTx);

  USB.VID(0x303A);
  USB.PID(0x81C0);
  USB.productName("link-sync MIDI");
  USB.manufacturerName("neon-link");
  g_midi.begin();
  USB.begin();

  rgb(0, 0, 8);
}

void loop() {
  while (Serial1.available() > 0) {
    g_midi.write(static_cast<uint8_t>(Serial1.read()));
    led_kick();
  }

  midiEventPacket_t pkt{};
  if (g_midi.readPacket(&pkt)) {
    const uint8_t cin = static_cast<uint8_t>(pkt.header & 0x0F);
    const int n = kCinBytes[cin];
    if (n > 0) {
      const uint8_t bytes[3] = {pkt.byte1, pkt.byte2, pkt.byte3};
      Serial1.write(bytes, static_cast<size_t>(n));
      led_kick();
    }
  }

  led_poll();
}
