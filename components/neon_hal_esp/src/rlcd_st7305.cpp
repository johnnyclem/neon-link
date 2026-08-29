// ST7305 reflective LCD driver for the Waveshare ESP32-S3-RLCD-4.2.
// Register table and command order follow the vendor init for this
// glass (SolarOS src/drivers/rlcd_st7305.c, Waveshare demo): the
// voltage/gate values at the top, SLPOUT with the long post-reset
// delay in the middle, and the mirrored 0x3C-addr CASET at the end
// are all load-bearing — the panel shows nothing if they drift.

#include "halesp/rlcd_st7305.hpp"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "neon/gfx/rlcd_pack.hpp"

#include <cstring>
#include <new>

namespace halesp {

namespace {

const char* kTag = "rlcd7305";

constexpr int kSpiHz = 24000000;
constexpr size_t kMaxChunk = 4092;
// Merge dirty row spans separated by fewer rows than one RASET+0x2C
// header round-trip is worth.
constexpr int kSpanGap = 8;

int g_cs = -1;
int g_dc = -1;
int g_rst = -1;
spi_device_handle_t g_spi = nullptr;
bool g_ready = false;
bool g_hpm = true;
uint8_t* g_shadow = nullptr;
bool g_shadow_valid = false;

void delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void as_out(int pin, int level) {
  gpio_reset_pin(static_cast<gpio_num_t>(pin));
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
}

void spi_bytes(const uint8_t* p, size_t n) {
  while (n > 0) {
    const size_t chunk = n > kMaxChunk ? kMaxChunk : n;
    spi_transaction_t t = {};
    t.length = chunk * 8;
    t.tx_buffer = p;
    spi_device_polling_transmit(g_spi, &t);
    p += chunk;
    n -= chunk;
  }
}

void cmd_data(uint8_t c, const uint8_t* d, size_t n) {
  gpio_set_level(static_cast<gpio_num_t>(g_dc), 0);
  gpio_set_level(static_cast<gpio_num_t>(g_cs), 0);
  spi_bytes(&c, 1);
  if (n > 0) {
    gpio_set_level(static_cast<gpio_num_t>(g_dc), 1);
    spi_bytes(d, n);
  }
  gpio_set_level(static_cast<gpio_num_t>(g_cs), 1);
}

void cmd(uint8_t c) { cmd_data(c, nullptr, 0); }

// One RAM burst: 0x2C with CS held low across the whole payload.
void ram_write(const uint8_t* d, size_t n) {
  const uint8_t c = 0x2c;
  gpio_set_level(static_cast<gpio_num_t>(g_dc), 0);
  gpio_set_level(static_cast<gpio_num_t>(g_cs), 0);
  spi_bytes(&c, 1);
  gpio_set_level(static_cast<gpio_num_t>(g_dc), 1);
  spi_bytes(d, n);
  gpio_set_level(static_cast<gpio_num_t>(g_cs), 1);
}

void reset_hw() {
  gpio_set_level(static_cast<gpio_num_t>(g_rst), 1);
  delay_ms(50);
  gpio_set_level(static_cast<gpio_num_t>(g_rst), 0);
  delay_ms(20);
  gpio_set_level(static_cast<gpio_num_t>(g_rst), 1);
  delay_ms(50);
}

void set_window(uint8_t row0, uint8_t row1) {
  // CASET is mirrored on this glass: {0x3C - end, 0x3C - start}.
  const uint8_t col[] = {neon::rlcd::kCasetLo, neon::rlcd::kCasetHi};
  cmd_data(0x2a, col, sizeof(col));
  const uint8_t row[] = {row0, row1};
  cmd_data(0x2b, row, sizeof(row));
}

void apply_profile() {
  static const uint8_t d6[] = {0x17, 0x02};  // NVM load
  static const uint8_t d1[] = {0x01};        // booster
  static const uint8_t c0[] = {0x11, 0x04};  // gate voltage
  static const uint8_t c1[] = {0x69, 0x69, 0x69, 0x69};  // VSHP
  static const uint8_t c2[] = {0x19, 0x19, 0x19, 0x19};  // VSLP
  static const uint8_t c4[] = {0x4b, 0x4b, 0x4b, 0x4b};  // VSHN
  static const uint8_t c5[] = {0x19, 0x19, 0x19, 0x19};  // VSLN
  // OSC 0xA6 + FRCTRL HFRA(0x10) = 32 Hz HPM scan; LFRA=2 = 1 Hz LPM.
  static const uint8_t d8[] = {0xa6, 0xe9};
  static const uint8_t b2[] = {0x12};
  static const uint8_t b3[] = {0xe5, 0xf6, 0x05, 0x46, 0x77,
                               0x77, 0x77, 0x77, 0x76, 0x45};
  static const uint8_t b4[] = {0x05, 0x46, 0x77, 0x77,
                               0x77, 0x77, 0x76, 0x45};
  static const uint8_t g62[] = {0x32, 0x03, 0x1f};  // gate timing
  static const uint8_t b7[] = {0x13};               // source EQ
  static const uint8_t b0[] = {0x64};               // duty
  static const uint8_t c9[] = {0x00};               // source voltage sel
  static const uint8_t m36[] = {0x48};              // MADCTL
  static const uint8_t m3a[] = {0x11};              // COLMOD: 1 bpp
  static const uint8_t b9[] = {0x20};               // gamma mode
  static const uint8_t b8[] = {0x29};               // panel setting
  static const uint8_t m35[] = {0x00};              // TE on
  static const uint8_t d0[] = {0xff};               // auto power down

  cmd_data(0xd6, d6, sizeof(d6));
  cmd_data(0xd1, d1, sizeof(d1));
  cmd_data(0xc0, c0, sizeof(c0));
  cmd_data(0xc1, c1, sizeof(c1));
  cmd_data(0xc2, c2, sizeof(c2));
  cmd_data(0xc4, c4, sizeof(c4));
  cmd_data(0xc5, c5, sizeof(c5));
  cmd_data(0xd8, d8, sizeof(d8));
  cmd_data(0xb2, b2, sizeof(b2));
  cmd_data(0xb3, b3, sizeof(b3));
  cmd_data(0xb4, b4, sizeof(b4));
  cmd_data(0x62, g62, sizeof(g62));
  cmd_data(0xb7, b7, sizeof(b7));
  cmd_data(0xb0, b0, sizeof(b0));
  cmd(0x11);  // SLPOUT
  delay_ms(120);
  cmd_data(0xc9, c9, sizeof(c9));
  cmd_data(0x36, m36, sizeof(m36));
  cmd_data(0x3a, m3a, sizeof(m3a));
  cmd_data(0xb9, b9, sizeof(b9));
  cmd_data(0xb8, b8, sizeof(b8));
  // INVON: data 1 = reflective/white, matching the canvas polarity.
  cmd(0x21);
  set_window(0x00, static_cast<uint8_t>(neon::rlcd::kPackedRows - 1));
  cmd_data(0x35, m35, sizeof(m35));
  cmd_data(0xd0, d0, sizeof(d0));
  cmd(g_hpm ? 0x38 : 0x39);
  cmd(0x29);  // DISPON
}

}  // namespace

bool rlcd_st7305_init(int sck, int mosi, int cs, int dc, int rst) {
  if (sck < 0 || mosi < 0 || cs < 0 || dc < 0 || rst < 0) {
    ESP_LOGE(kTag, "rlcd pins incomplete");
    return false;
  }
  g_cs = cs;
  g_dc = dc;
  g_rst = rst;
  as_out(cs, 1);
  as_out(dc, 1);
  as_out(rst, 1);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = mosi;
  bus.miso_io_num = -1;
  bus.sclk_io_num = sck;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = static_cast<int>(kMaxChunk);
  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err == ESP_ERR_INVALID_STATE) {
    spi_bus_free(SPI2_HOST);
    err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  }
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "SPI bus init failed (%s)", esp_err_to_name(err));
    return false;
  }
  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = kSpiHz;
  dev.mode = 0;
  dev.spics_io_num = -1;  // manual CS — DC must be valid before CS falls
  dev.queue_size = 1;
  if (spi_bus_add_device(SPI2_HOST, &dev, &g_spi) != ESP_OK) {
    ESP_LOGE(kTag, "SPI device add failed");
    return false;
  }

  if (g_shadow == nullptr) {
    g_shadow = new (std::nothrow) uint8_t[neon::rlcd::kPackedSize];
  }
  g_shadow_valid = false;

  reset_hw();
  apply_profile();
  g_ready = true;
  ESP_LOGI(kTag, "ST7305 up SCK=%d MOSI=%d CS=%d DC=%d RST=%d", sck, mosi, cs,
           dc, rst);
  return true;
}

void rlcd_st7305_present(const uint8_t* packed) {
  if (!g_ready || packed == nullptr) {
    return;
  }
  constexpr int kRows = neon::rlcd::kPackedRows;
  constexpr int kRowBytes = neon::rlcd::kPackedRowBytes;

  const bool diff = g_shadow != nullptr && g_shadow_valid;
  int r = 0;
  while (r < kRows) {
    int r0 = r;
    if (diff) {
      while (r0 < kRows &&
             std::memcmp(packed + static_cast<size_t>(r0) * kRowBytes,
                         g_shadow + static_cast<size_t>(r0) * kRowBytes,
                         kRowBytes) == 0) {
        ++r0;
      }
      if (r0 >= kRows) {
        break;
      }
    }
    int r1 = diff ? r0 : kRows - 1;
    if (diff) {
      int clean = 0;
      for (int rr = r0 + 1; rr < kRows && clean <= kSpanGap; ++rr) {
        if (std::memcmp(packed + static_cast<size_t>(rr) * kRowBytes,
                        g_shadow + static_cast<size_t>(rr) * kRowBytes,
                        kRowBytes) == 0) {
          ++clean;
        } else {
          r1 = rr;
          clean = 0;
        }
      }
    }
    set_window(static_cast<uint8_t>(r0), static_cast<uint8_t>(r1));
    ram_write(packed + static_cast<size_t>(r0) * kRowBytes,
              static_cast<size_t>(r1 - r0 + 1) * kRowBytes);
    r = r1 + 1;
  }

  if (g_shadow != nullptr) {
    std::memcpy(g_shadow, packed, neon::rlcd::kPackedSize);
    g_shadow_valid = true;
  }
}

void rlcd_st7305_set_power(bool hpm) {
  if (!g_ready || hpm == g_hpm) {
    g_hpm = hpm;
    return;
  }
  g_hpm = hpm;
  cmd(hpm ? 0x38 : 0x39);
}

void rlcd_st7305_display_off() {
  if (!g_ready) {
    return;
  }
  cmd(0x28);  // DISPOFF
  cmd(0x10);  // SLPIN
  g_ready = false;
  g_shadow_valid = false;
}

}  // namespace halesp
