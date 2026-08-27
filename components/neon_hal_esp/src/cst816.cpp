#include "halesp/cst816.hpp"

#if CONFIG_NEON_BOARD_LINKSYNC_MATOUCH

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/i2c_bus.hpp"

namespace halesp {
namespace {

const char* kTag = "cst816";

constexpr uint8_t kAddr = 0x15;
constexpr int kPanel = 240;

bool g_ok = false;

}  // namespace

bool cst816_init(int sda_gpio, int scl_gpio, int rst_gpio) {
  if (rst_gpio >= 0) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ull << static_cast<unsigned>(rst_gpio);
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level(static_cast<gpio_num_t>(rst_gpio), 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(static_cast<gpio_num_t>(rst_gpio), 1);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (!i2c_bus_init(sda_gpio, scl_gpio)) {
    ESP_LOGW(kTag, "i2c bus init failed");
    return false;
  }
  g_ok = i2c_probe(kAddr);
  ESP_LOGI(kTag, "CST816 @ 0x15 %s (sda=%d scl=%d)", g_ok ? "ACK" : "nack",
           sda_gpio, scl_gpio);
  return g_ok;
}

bool cst816_poll(int* x, int* y) {
  if (!g_ok) {
    return false;
  }
  // Registers 0x02..0x06: finger count, then X hi/lo, Y hi/lo.
  const uint8_t reg = 0x02;
  uint8_t buf[5] = {};
  if (!i2c_write_read(kAddr, &reg, 1, buf, sizeof(buf))) {
    return false;
  }
  if ((buf[0] & 0x0f) == 0) {
    return false;  // no finger down
  }
  int px = ((buf[1] & 0x0f) << 8) | buf[2];
  int py = ((buf[3] & 0x0f) << 8) | buf[4];
  if (px < 0 || px >= kPanel || py < 0 || py >= kPanel) {
    return false;
  }
  // The GC9A01 is X-mirrored (see lcd_gc9a01_init); mirror touch to match.
  if (x != nullptr) {
    *x = (kPanel - 1) - px;
  }
  if (y != nullptr) {
    *y = py;
  }
  return true;
}

}  // namespace halesp

#endif  // CONFIG_NEON_BOARD_LINKSYNC_MATOUCH
