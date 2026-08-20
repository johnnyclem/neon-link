#include "halesp/i2c_bus.hpp"

#include <cstring>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace halesp {

namespace {
const char* kTag = "i2c_bus";
i2c_master_bus_handle_t g_bus = nullptr;
SemaphoreHandle_t g_lock = nullptr;

// Cache a small set of device handles so we don't thrash add/remove under
// concurrent ADS1015 + GP8413 + OLED traffic (that race was corrupting the
// bus and making the Grove OLED look missing).
constexpr int kMaxDevs = 8;
struct DevSlot {
  uint8_t addr = 0;
  i2c_master_dev_handle_t handle = nullptr;
};
DevSlot g_devs[kMaxDevs] = {};

void ensure_lock() {
  if (g_lock == nullptr) {
    g_lock = xSemaphoreCreateMutex();
  }
}

void lock() {
  ensure_lock();
  xSemaphoreTake(g_lock, portMAX_DELAY);
}

void unlock() { xSemaphoreGive(g_lock); }

i2c_master_dev_handle_t get_dev(uint8_t addr7) {
  for (auto& s : g_devs) {
    if (s.handle != nullptr && s.addr == addr7) {
      return s.handle;
    }
  }
  // Free slot?
  for (auto& s : g_devs) {
    if (s.handle == nullptr) {
      i2c_device_config_t dev = {};
      dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
      dev.device_address = addr7;
      dev.scl_speed_hz = 400000;
      if (i2c_master_bus_add_device(g_bus, &dev, &s.handle) != ESP_OK) {
        return nullptr;
      }
      s.addr = addr7;
      return s.handle;
    }
  }
  // Full: drop slot 0 and reuse (OLED + DAC + ADC + expander need 4).
  if (g_devs[0].handle) {
    i2c_master_bus_rm_device(g_devs[0].handle);
    g_devs[0].handle = nullptr;
  }
  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = addr7;
  dev.scl_speed_hz = 400000;
  if (i2c_master_bus_add_device(g_bus, &dev, &g_devs[0].handle) != ESP_OK) {
    return nullptr;
  }
  g_devs[0].addr = addr7;
  return g_devs[0].handle;
}
}  // namespace

bool i2c_bus_init(int sda_gpio, int scl_gpio, uint32_t hz) {
  (void)hz;
  ensure_lock();
  lock();
  if (g_bus != nullptr) {
    unlock();
    return true;
  }
  if (sda_gpio < 0 || scl_gpio < 0) {
    ESP_LOGE(kTag, "invalid I2C pins sda=%d scl=%d", sda_gpio, scl_gpio);
    unlock();
    return false;
  }
  // Warm reset leaves the Grove SH1107 powered and sometimes holding
  // SDA. Nine SCL clocks plus a STOP unsticks it before the driver
  // takes the pins — otherwise probe ACKs and init "succeeds" with a
  // blank glass.
  {
    gpio_config_t io = {};
    io.pin_bit_mask =
        (1ull << static_cast<unsigned>(sda_gpio)) |
        (1ull << static_cast<unsigned>(scl_gpio));
    io.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);
    gpio_set_level(static_cast<gpio_num_t>(scl_gpio), 1);
    gpio_set_level(static_cast<gpio_num_t>(sda_gpio), 1);
    if (gpio_get_level(static_cast<gpio_num_t>(sda_gpio)) == 0) {
      ESP_LOGW(kTag, "SDA stuck low; clocking SCL to recover");
      for (int i = 0; i < 9; ++i) {
        gpio_set_level(static_cast<gpio_num_t>(scl_gpio), 0);
        esp_rom_delay_us(5);
        gpio_set_level(static_cast<gpio_num_t>(scl_gpio), 1);
        esp_rom_delay_us(5);
      }
      gpio_set_level(static_cast<gpio_num_t>(sda_gpio), 0);
      esp_rom_delay_us(5);
      gpio_set_level(static_cast<gpio_num_t>(scl_gpio), 1);
      esp_rom_delay_us(5);
      gpio_set_level(static_cast<gpio_num_t>(sda_gpio), 1);
    }
  }
  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = I2C_NUM_0;
  bus_cfg.sda_io_num = static_cast<gpio_num_t>(sda_gpio);
  bus_cfg.scl_io_num = static_cast<gpio_num_t>(scl_gpio);
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = true;
  if (i2c_new_master_bus(&bus_cfg, &g_bus) != ESP_OK) {
    ESP_LOGE(kTag, "i2c_new_master_bus failed");
    g_bus = nullptr;
    unlock();
    return false;
  }
  ESP_LOGI(kTag, "I2C master on SDA=%d SCL=%d (mutexed)", sda_gpio, scl_gpio);
  unlock();
  const bool oled_3d = i2c_probe(0x3d, 20);
  const bool oled_3c = i2c_probe(0x3c, 20);
  const bool es8311 = i2c_probe(0x18, 20);
  const bool stc8 = i2c_probe(0x2f, 20);
  ESP_LOGI(kTag, "I2C probe OLED 0x3d=%s 0x3c=%s ES8311 0x18=%s STC8 0x2f=%s",
           oled_3d ? "ACK" : "nack", oled_3c ? "ACK" : "nack",
           es8311 ? "ACK" : "nack", stc8 ? "ACK" : "nack");
  return true;
}

i2c_master_bus_handle_t i2c_bus() { return g_bus; }

bool i2c_probe(uint8_t addr7, int timeout_ms) {
  if (g_bus == nullptr) {
    return false;
  }
  lock();
  // IDF 5.3+: clean ACK probe, no payload, no device handle churn.
  const esp_err_t err = i2c_master_probe(g_bus, addr7, timeout_ms);
  unlock();
  return err == ESP_OK;
}

bool i2c_write(uint8_t addr7, const uint8_t* data, size_t len,
               int timeout_ms) {
  if (g_bus == nullptr || data == nullptr || len == 0) {
    return false;
  }
  lock();
  i2c_master_dev_handle_t handle = get_dev(addr7);
  if (handle == nullptr) {
    unlock();
    return false;
  }
  const esp_err_t err =
      i2c_master_transmit(handle, data, len, timeout_ms);
  unlock();
  return err == ESP_OK;
}

bool i2c_write_read(uint8_t addr7, const uint8_t* wr, size_t wr_len,
                    uint8_t* rd, size_t rd_len, int timeout_ms) {
  if (g_bus == nullptr || rd == nullptr || rd_len == 0) {
    return false;
  }
  lock();
  i2c_master_dev_handle_t handle = get_dev(addr7);
  if (handle == nullptr) {
    unlock();
    return false;
  }
  esp_err_t err;
  if (wr != nullptr && wr_len > 0) {
    err = i2c_master_transmit_receive(handle, wr, wr_len, rd, rd_len,
                                      timeout_ms);
  } else {
    err = i2c_master_receive(handle, rd, rd_len, timeout_ms);
  }
  unlock();
  return err == ESP_OK;
}

bool i2c_write_stop_read(uint8_t addr7, const uint8_t* wr, size_t wr_len,
                         uint8_t* rd, size_t rd_len, int timeout_ms) {
  if (g_bus == nullptr || wr == nullptr || wr_len == 0 || rd == nullptr ||
      rd_len == 0) {
    return false;
  }
  lock();
  i2c_master_dev_handle_t handle = get_dev(addr7);
  if (handle == nullptr) {
    unlock();
    return false;
  }
  esp_err_t err = i2c_master_transmit(handle, wr, wr_len, timeout_ms);
  if (err == ESP_OK) {
    err = i2c_master_receive(handle, rd, rd_len, timeout_ms);
  }
  unlock();
  return err == ESP_OK;
}

}  // namespace halesp
