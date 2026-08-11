#include "halesp/gp8413.hpp"

#include "esp_log.h"
#include "halesp/i2c_bus.hpp"

namespace halesp {

namespace {
const char* kTag = "gp8413";
constexpr uint8_t kAddr = 0x58;
constexpr uint8_t kRegCh0 = 0x02;
constexpr uint8_t kRegCh1 = 0x04;
bool g_ok = false;

// volts ∈ [-10, +10] → 15-bit code (matches tulip/shared amyboard path).
uint16_t volts_to_code(float volts) {
  if (volts < -10.0f) volts = -10.0f;
  if (volts > 10.0f) volts = 10.0f;
  int val = static_cast<int>(((volts + 10.0f) / 20.0f) * 0x8000);
  if (val < 0) val = 0;
  if (val > 0x7fff) val = 0x7fff;
  return static_cast<uint16_t>(val);
}
}  // namespace

bool gp8413_init() {
  if (i2c_bus() == nullptr) {
    ESP_LOGE(kTag, "I2C bus not ready");
    return false;
  }
  // Probe with a mid-scale write on both channels (0 V).
  g_ok = gp8413_set_volts(0, 0.0f) && gp8413_set_volts(1, 0.0f);
  if (g_ok) {
    ESP_LOGI(kTag, "GP8413 ready at 0x%02x", kAddr);
  } else {
    ESP_LOGW(kTag, "GP8413 not responding at 0x%02x", kAddr);
  }
  return g_ok;
}

bool gp8413_set_volts(uint8_t channel, float volts) {
  if (channel > 1) {
    return false;
  }
  const uint16_t code = volts_to_code(volts);
  // Little-endian payload after the register byte (AMYboard convention).
  const uint8_t reg = (channel == 0) ? kRegCh0 : kRegCh1;
  const uint8_t bytes[3] = {reg, static_cast<uint8_t>(code & 0xff),
                            static_cast<uint8_t>((code >> 8) & 0xff)};
  return i2c_write(kAddr, bytes, sizeof(bytes));
}

bool gp8413_set_ratio(uint8_t channel, uint16_t ratio_q16, float lo_v,
                      float hi_v) {
  const float t = static_cast<float>(ratio_q16) / 65535.0f;
  return gp8413_set_volts(channel, lo_v + t * (hi_v - lo_v));
}

}  // namespace halesp
