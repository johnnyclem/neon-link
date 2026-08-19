#include "halesp/es8311.hpp"

#include "sdkconfig.h"

#if CONFIG_NEON_BOARD_P4DEVKIT

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/i2c_bus.hpp"

namespace halesp {

namespace {

const char* kTag = "es8311";
constexpr uint8_t kAddr = 0x18;

bool g_up = false;

bool wr(uint8_t reg, uint8_t val) {
  const uint8_t pkt[2] = {reg, val};
  return i2c_write(kAddr, pkt, 2, 100);
}

bool rd(uint8_t reg, uint8_t* val) {
  return i2c_write_read(kAddr, &reg, 1, val, 1, 100);
}

void pa_set(int level) {
  if (kPinI2sPa < 0) {
    return;
  }
  gpio_set_level(static_cast<gpio_num_t>(kPinI2sPa), level);
}

void pa_init() {
  if (kPinI2sPa < 0) {
    return;
  }
  const gpio_num_t pin = static_cast<gpio_num_t>(kPinI2sPa);
  gpio_reset_pin(pin);
  gpio_set_direction(pin, GPIO_MODE_OUTPUT);
  gpio_set_level(pin, 0);
}

}  // namespace

bool es8311_start() {
  if (g_up) {
    return true;
  }
  if (i2c_bus() == nullptr) {
    ESP_LOGW(kTag, "I2C bus not ready");
    return false;
  }
  if (!i2c_probe(kAddr, 50)) {
    ESP_LOGW(kTag, "no ACK at 0x%02x", kAddr);
    return false;
  }

  pa_init();

  // Reset + CSM power-on (Espressif es8311_init).
  if (!wr(0x00, 0x1F)) {
    ESP_LOGW(kTag, "reset write failed");
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(20));
  if (!wr(0x00, 0x00) || !wr(0x00, 0x80)) {
    ESP_LOGW(kTag, "power-on write failed");
    return false;
  }

  // 48 kHz, 12.288 MHz MCLK from pin, 256fs, BCLK = MCLK/4.
  // Coeff row: {12288000, 48000, pre_div=1, mult=1x, adc/dac_div=1,
  // ss, lrck=0x00FF, bclk_div=4, osr=0x10}.
  const bool clocks =
      wr(0x01, 0x3F) && wr(0x02, 0x00) && wr(0x03, 0x10) && wr(0x04, 0x10) &&
      wr(0x05, 0x00) && wr(0x06, 0x03) && wr(0x07, 0x00) && wr(0x08, 0xFF);
  // 32-bit I2S (Philips) on both SDP sides — matches the packed slots.
  const bool fmt = wr(0x09, 0x10) && wr(0x0A, 0x10);
  const bool analog = wr(0x0D, 0x01) && wr(0x0E, 0x02) && wr(0x12, 0x00) &&
                      wr(0x13, 0x10) && wr(0x1C, 0x6A) && wr(0x37, 0x08);
  // ~75% DAC volume, unmuted.
  const bool dac = wr(0x32, 0xBF) && wr(0x31, 0x00);
  if (!clocks || !fmt || !analog || !dac) {
    ESP_LOGW(kTag, "register program failed");
    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(15));
  pa_set(1);
  g_up = true;

  uint8_t id1 = 0;
  uint8_t id2 = 0;
  rd(0xFD, &id1);
  rd(0xFE, &id2);
  ESP_LOGI(kTag, "up @ 0x%02x id=%02x%02x PA=%d", kAddr, id1, id2, kPinI2sPa);
  return true;
}

void es8311_stop() {
  if (g_up) {
    wr(0x31, 0x60);  // mute L+R
  }
  pa_set(0);
  g_up = false;
}

}  // namespace halesp

#else  // !CONFIG_NEON_BOARD_P4DEVKIT

namespace halesp {

bool es8311_start() { return true; }

void es8311_stop() {}

}  // namespace halesp

#endif
