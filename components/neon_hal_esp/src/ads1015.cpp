#include "halesp/ads1015.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/i2c_bus.hpp"

namespace halesp {

namespace {
const char* kTag = "ads1015";
constexpr uint8_t kAddr = 0x48;
constexpr uint8_t kRegConvert = 0x00;
constexpr uint8_t kRegConfig = 0x01;

// Matches amyboard_support.c: bench-calibrated loopback.
constexpr int32_t kRawAtNeg5 = 10064;
constexpr int32_t kRawAtPos5 = 30096;

bool g_ok = false;

bool write_reg16(uint8_t reg, uint16_t value) {
  const uint8_t bytes[3] = {reg, static_cast<uint8_t>(value >> 8),
                            static_cast<uint8_t>(value & 0xff)};
  return i2c_write(kAddr, bytes, sizeof(bytes));
}

bool read_reg16(uint8_t reg, uint16_t* out) {
  uint8_t rd[2] = {};
  if (!i2c_write_read(kAddr, &reg, 1, rd, 2)) {
    return false;
  }
  *out = (static_cast<uint16_t>(rd[0]) << 8) | rd[1];
  return true;
}
}  // namespace

bool ads1015_init() {
  if (i2c_bus() == nullptr) {
    ESP_LOGE(kTag, "I2C bus not ready");
    return false;
  }
  float probe = 0;
  g_ok = ads1015_read_volts(0, &probe);
  if (g_ok) {
    ESP_LOGI(kTag, "ADS1015 ready (ch0=%.2f V)", probe);
  } else {
    ESP_LOGW(kTag, "ADS1015 not responding at 0x%02x", kAddr);
  }
  return g_ok;
}

bool ads1015_read_volts(uint8_t channel, float* volts) {
  if (channel > 1 || volts == nullptr) {
    return false;
  }
  // Single-shot, ± 2.048 V, 3300 SPS, mux = AIN0/AIN1 vs GND.
  // Config bits match amyboard_support.c.
  constexpr uint16_t kBase =
      0x8000 |  // OS single
      0x0100 |  // single-shot mode
      0x00E0 |  // 3300 SPS
      0x0400 |  // PGA ±2.048 V
      0x0003;   // disable comparator
  const uint16_t mux = static_cast<uint16_t>(0x4000 + (channel << 12));
  if (!write_reg16(kRegConfig, kBase | mux)) {
    return false;
  }
  // ~0.3 ms conversion; poll OS bit (1 = done) with a bound.
  for (int i = 0; i < 20; ++i) {
    uint16_t cfg = 0;
    if (!read_reg16(kRegConfig, &cfg)) {
      return false;
    }
    if (cfg & 0x8000) {
      break;
    }
    vTaskDelay(1);
  }
  uint16_t raw = 0;
  if (!read_reg16(kRegConvert, &raw)) {
    return false;
  }
  // Map [raw@-5V, raw@+5V] → [-5, +5] (same as stock firmware).
  *volts = (((static_cast<float>(static_cast<int32_t>(raw) - kRawAtNeg5)) /
             static_cast<float>(kRawAtPos5 - kRawAtNeg5)) *
            10.0f) -
           5.0f;
  return true;
}

}  // namespace halesp
