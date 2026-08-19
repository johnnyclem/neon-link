// NanoC6 UART MIDI ↔ BLE MIDI (+ USB CDC byte pipe for Hairless).
//
// ESP32-C6 USB-C is Serial/JTAG CDC only — no USB OTG — so this cannot
// enumerate as a class-compliant USB MIDI device. macOS sees BLE MIDI
// natively (Audio MIDI Setup). Hairless-midiserial talks to the CDC
// port at 115200 if you want a wired path.
//
// Grove HY2.0-4P: GND, 5V, G2 (TX), G1 (RX). CrowPanel IO21 → G1.

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>

namespace {

constexpr int kPinRx = 1;   // Grove yellow → CrowPanel MIDI TX (IO21)
constexpr int kPinTx = 2;   // Grove white  → optional MIDI OUT
constexpr int kPinLed = 7;  // onboard blue
constexpr int kPinBtn = 9;
constexpr uint32_t kUartBaud = 31250;
constexpr uint32_t kCdcBaud = 115200;
const char* kName = "link-sync MIDI";

const NimBLEUUID kMidiSvc("03b80e5a-ede8-4b33-a751-6ce34ec4c700");
const NimBLEUUID kMidiChr("7772e5db-3868-4112-a1a9-f2669d106bf3");

NimBLECharacteristic* g_chr = nullptr;
volatile bool g_ble_connected = false;
uint32_t g_led_until_ms = 0;

void led_kick() {
  digitalWrite(kPinLed, HIGH);
  g_led_until_ms = millis() + 8;
}

void led_poll() {
  if (g_led_until_ms != 0 && static_cast<int32_t>(millis() - g_led_until_ms) >= 0) {
    digitalWrite(kPinLed, LOW);
    g_led_until_ms = 0;
  }
}

void uart_write(const uint8_t* p, size_t n) {
  if (n == 0) {
    return;
  }
  Serial1.write(p, n);
  led_kick();
}

void ble_send(const uint8_t* midi, size_t n) {
  if (!g_ble_connected || g_chr == nullptr || n == 0 || n > 16) {
    return;
  }
  const uint32_t ts = millis() & 0x1fff;
  uint8_t pkt[18];
  pkt[0] = static_cast<uint8_t>(0x80u | ((ts >> 7) & 0x3fu));
  pkt[1] = static_cast<uint8_t>(0x80u | (ts & 0x7fu));
  memcpy(pkt + 2, midi, n);
  g_chr->setValue(pkt, n + 2);
  g_chr->notify();
  led_kick();
}

void emit_host(const uint8_t* midi, size_t n) {
  ble_send(midi, n);
  Serial.write(midi, n);
}

struct UartParser {
  uint8_t buf[3] = {};
  uint8_t need = 0;
  uint8_t have = 0;
  uint8_t running = 0;

  static uint8_t data_bytes(uint8_t status) {
    if (status >= 0xF8) {
      return 0;
    }
    switch (status & 0xF0) {
      case 0xC0:
      case 0xD0:
        return 1;
      case 0xF0:
        if (status == 0xF1 || status == 0xF3) {
          return 1;
        }
        if (status == 0xF2) {
          return 2;
        }
        return 0;
      default:
        return 2;
    }
  }

  void push(uint8_t b) {
    if (b >= 0xF8) {
      emit_host(&b, 1);
      return;
    }
    if (b & 0x80) {
      if (b == 0xF0 || b == 0xF7) {
        running = 0;
        need = 0;
        have = 0;
        return;
      }
      running = b;
      buf[0] = b;
      have = 1;
      need = static_cast<uint8_t>(1 + data_bytes(b));
      if (need == 1) {
        emit_host(buf, 1);
        have = 0;
      }
      return;
    }
    if (have == 0) {
      if (running == 0) {
        return;
      }
      buf[0] = running;
      have = 1;
      need = static_cast<uint8_t>(1 + data_bytes(running));
    }
    if (have < sizeof(buf)) {
      buf[have++] = b;
    }
    if (have >= need && need != 0) {
      emit_host(buf, need);
      have = 0;
    }
  }
} g_uart;

// After the header, the first high-bit byte is a timestamp. Later high-bit
// bytes are either another timestamp (when the next byte is also a status)
// or a MIDI status / realtime.
void ble_rx_bytes(const uint8_t* d, size_t n) {
  if (n < 2) {
    return;
  }
  size_t i = 1;
  if (d[i] & 0x80) {
    ++i;
  }
  while (i < n) {
    const uint8_t b = d[i];
    if ((b & 0x80) && b < 0xF8 && i + 1 < n && (d[i + 1] & 0x80) &&
        d[i + 1] < 0xF8) {
      ++i;
      continue;
    }
    uart_write(&b, 1);
    ++i;
  }
}

class ChrCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    auto v = c->getValue();
    if (v.length() > 0) {
      ble_rx_bytes(reinterpret_cast<const uint8_t*>(v.data()), v.length());
    }
  }
};

class SrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override {
    g_ble_connected = true;
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    g_ble_connected = false;
    NimBLEDevice::startAdvertising();
  }
};

ChrCb g_chr_cb;
SrvCb g_srv_cb;

void ble_start() {
  NimBLEDevice::init(kName);
  NimBLEDevice::setPower(9);
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&g_srv_cb);
  NimBLEService* svc = server->createService(kMidiSvc);
  g_chr = svc->createCharacteristic(
      kMidiChr, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
                    NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  g_chr->setCallbacks(&g_chr_cb);
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(kMidiSvc);
  adv->setName(kName);
  adv->start();
}

}  // namespace

void setup() {
  pinMode(kPinLed, OUTPUT);
  digitalWrite(kPinLed, LOW);
  pinMode(kPinBtn, INPUT_PULLUP);

  Serial.begin(kCdcBaud);
  Serial1.begin(kUartBaud, SERIAL_8N1, kPinRx, kPinTx);
  ble_start();

  for (int i = 0; i < 3; ++i) {
    digitalWrite(kPinLed, HIGH);
    delay(60);
    digitalWrite(kPinLed, LOW);
    delay(80);
  }
}

void loop() {
  while (Serial1.available() > 0) {
    g_uart.push(static_cast<uint8_t>(Serial1.read()));
  }
  while (Serial.available() > 0) {
    const uint8_t b = static_cast<uint8_t>(Serial.read());
    uart_write(&b, 1);
  }
  led_poll();
}
