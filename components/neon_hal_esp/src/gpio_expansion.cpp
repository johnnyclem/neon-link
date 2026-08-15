#include "halesp/gpio_expansion.hpp"

#include <cstdio>

#include "esp_log.h"
#include "halesp/i2c_bus.hpp"

namespace halesp {

namespace {
const char* kTag = "gpio_exp";

constexpr uint8_t kRegVersion = 0x00;
constexpr uint8_t kRegIoMode = 0x01;
constexpr uint8_t kRegAnalog = 0x10;
constexpr uint8_t kRegDigital = 0x40;

bool g_present = false;
int g_last_adc[kGpioExpPins] = {-1, -1, -1, -1, -1, -1, -1, -1};

bool valid_pin(int pin) { return pin >= 0 && pin < kGpioExpPins; }

// Fail-fast: encoder polling shares this bus with the OLED.
constexpr int kTimeoutMs = 20;
}  // namespace

bool gpio_exp_init() {
  if (g_present) {
    return true;
  }
  if (i2c_bus() == nullptr) {
    return false;
  }
  // One-shot scan so a missing 0x24 (solder-pad address) is obvious.
  char seen[80];
  size_t n = 0;
  seen[0] = '\0';
  for (uint8_t a = 0x08; a <= 0x77; ++a) {
    if (i2c_probe(a, 10)) {
      n += static_cast<size_t>(
          snprintf(seen + n, sizeof(seen) - n, n ? " %02x" : "%02x", a));
      if (n >= sizeof(seen) - 4) {
        break;
      }
    }
  }
  ESP_LOGI(kTag, "I2C scan:%s%s", n ? " " : " (none)", seen);

  if (!i2c_probe(kGpioExpAddr, 50)) {
    ESP_LOGW(kTag, "no expander at 0x%02x", kGpioExpAddr);
    return false;
  }
  uint8_t ver = 0;
  const uint8_t reg = kRegVersion;
  if (i2c_write_stop_read(kGpioExpAddr, &reg, 1, &ver, 1, kTimeoutMs)) {
    ESP_LOGI(kTag, "NULLLAB GPIO expander @ 0x%02x ver=0x%02x", kGpioExpAddr,
             ver);
  } else {
    ESP_LOGI(kTag, "NULLLAB GPIO expander @ 0x%02x", kGpioExpAddr);
  }
  for (int i = 0; i < kGpioExpPins; ++i) {
    g_last_adc[i] = -1;
  }
  g_present = true;
  return true;
}

bool gpio_exp_present() { return g_present; }

bool gpio_exp_set_mode(int pin, GpioExpMode mode) {
  if (!g_present || !valid_pin(pin)) {
    return false;
  }
  if (mode == GpioExpMode::kPwm && pin != 1 && pin != 2) {
    return false;
  }
  const uint8_t pkt[2] = {static_cast<uint8_t>(kRegIoMode + pin),
                          static_cast<uint8_t>(mode)};
  return i2c_write(kGpioExpAddr, pkt, sizeof(pkt), kTimeoutMs);
}

bool gpio_exp_set_level(int pin, uint8_t level) {
  if (!g_present || !valid_pin(pin)) {
    return false;
  }
  const uint8_t pkt[2] = {static_cast<uint8_t>(kRegDigital + pin),
                          static_cast<uint8_t>(level ? 1 : 0)};
  return i2c_write(kGpioExpAddr, pkt, sizeof(pkt), kTimeoutMs);
}

bool gpio_exp_get_level(int pin, uint8_t* level) {
  if (!g_present || !valid_pin(pin) || level == nullptr) {
    return false;
  }
  const uint8_t reg = static_cast<uint8_t>(kRegDigital + pin);
  uint8_t v = 0;
  if (!i2c_write_stop_read(kGpioExpAddr, &reg, 1, &v, 1, kTimeoutMs)) {
    return false;
  }
  *level = v ? 1 : 0;
  return true;
}

bool gpio_exp_adc(int pin, uint16_t* value) {
  if (!g_present || !valid_pin(pin) || value == nullptr) {
    return false;
  }
  const uint8_t reg =
      static_cast<uint8_t>(kRegAnalog + pin * sizeof(uint16_t));
  uint8_t rd[2] = {};
  if (!i2c_write_stop_read(kGpioExpAddr, &reg, 1, rd, 2, kTimeoutMs)) {
    return false;
  }
  const uint16_t v =
      static_cast<uint16_t>(rd[0] | (static_cast<uint16_t>(rd[1]) << 8));
  *value = v;
  g_last_adc[pin] = static_cast<int>(v);
  return true;
}

int gpio_exp_last_adc(int pin) {
  if (!valid_pin(pin)) {
    return -1;
  }
  return g_last_adc[pin];
}

}  // namespace halesp
