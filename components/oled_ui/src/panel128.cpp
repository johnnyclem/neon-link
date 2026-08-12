// AMYboard 128×128 OLED support.
//
// Matches the stock MicroPython path (amyboard.init_display):
//   1. SSD1327 @ 0x3d  — Adafruit 1.5" grayscale STEMMA QT
//   2. SH1107  @ 0x3c  — generic 128×128 mono I2C modules
//   3. SSD1306 @ 0x3c  — classic 128×64 (top half of the FB)
// Optional SPI SH1107 when kPinDispSpi* pins are wired (see board_pins.h).

#include "oledui/panel128.hpp"

#include <cstdio>
#include <cstring>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/i2c_bus.hpp"
#include "sdkconfig.h"

namespace oledui {

namespace {

const char* kTag = "panel128";
PanelKind g_kind = PanelKind::kNone;

// ---- I2C helpers -------------------------------------------------------

// SSD1327 uses control 0x80 for commands (mcauser driver); SH1107/SSD1306
// accept 0x00. Pass the control byte per panel.
bool i2c_cmd_ctl(uint8_t addr, uint8_t ctl, uint8_t cmd) {
  const uint8_t buf[2] = {ctl, cmd};
  return halesp::i2c_write(addr, buf, 2);
}

bool i2c_cmd(uint8_t addr, uint8_t cmd) {
  // Default control for SH1107 / SSD1306.
  return i2c_cmd_ctl(addr, 0x00, cmd);
}

bool i2c_data(uint8_t addr, const uint8_t* data, size_t len) {
  // Chunk to keep each transaction under the I2C driver's comfort zone.
  // Prefix every chunk with 0x40 (data mode).
  constexpr size_t kChunk = 128;
  uint8_t pkt[1 + kChunk];
  pkt[0] = 0x40;
  size_t off = 0;
  while (off < len) {
    const size_t n = (len - off) > kChunk ? kChunk : (len - off);
    std::memcpy(pkt + 1, data + off, n);
    if (!halesp::i2c_write(addr, pkt, 1 + n, /*timeout_ms=*/100)) {
      return false;
    }
    off += n;
  }
  return true;
}

// ---- SSD1327 (128×128, 4-bit grayscale) --------------------------------

constexpr uint8_t kSsd1327Addr = 0x3d;

bool ssd1327_init() {
  // Sequence mirrored from tulip/shared/py/ssd1327.py (128×128). Each
  // multi-byte command is expanded so every entry is one I2C command byte.
  const uint8_t cmds[] = {
      0xFD, 0x12,        // unlock
      0xAE,              // display off
      0xA1, 0x00,        // start line
      0xA2, 0x00,        // offset
      0xA0, 0x51,        // remap
      0xA8, 0x7F,        // mux = 128-1
      0xAB, 0x01,        // enable internal VDD
      0xB1, 0x51,        // phase
      0xB3, 0x01,        // clock
      0xBC, 0x08,        // precharge
      0xBE, 0x07,        // VCOMH
      0xB6, 0x01,        // 2nd precharge
      0xD5, 0x62,        // function B
      0xB9,              // linear grayscale
      0x81, 0x7F,        // contrast
      0xA4,              // normal
      0x15, 0x00, 0x3F,  // col addr
      0x75, 0x00, 0x7F,  // row addr
      0x2E,              // deactivate scroll
      0xAF,              // display on
  };
  for (uint8_t c : cmds) {
    if (!i2c_cmd_ctl(kSsd1327Addr, 0x80, c)) {
      return false;
    }
  }
  ESP_LOGI(kTag, "SSD1327 @ 0x%02x (128x128 grayscale)", kSsd1327Addr);
  return true;
}

// Convert monochrome page FB → SSD1327 GS4_HMSB (2 pixels/byte, high nibble
// first). On pixels map to nibble 0xF (full white).
void ssd1327_pack(const neon::Framebuffer& fb, uint8_t* out /*8192*/) {
  size_t o = 0;
  for (int y = 0; y < 128; ++y) {
    for (int x = 0; x < 128; x += 2) {
      const uint8_t hi = fb.pixel(x, y) ? 0xF0 : 0x00;
      const uint8_t lo = fb.pixel(x + 1, y) ? 0x0F : 0x00;
      out[o++] = static_cast<uint8_t>(hi | lo);
    }
  }
}

bool ssd1327_flush(const neon::Framebuffer& fb) {
  static uint8_t gs[128 * 128 / 2];
  ssd1327_pack(fb, gs);
  // Reset window every frame (full refresh — simple + reliable).
  auto cmd = [](uint8_t c) { return i2c_cmd_ctl(kSsd1327Addr, 0x80, c); };
  if (!cmd(0x15) || !cmd(0x00) || !cmd(0x3F) || !cmd(0x75) || !cmd(0x00) ||
      !cmd(0x7F)) {
    return false;
  }
  return i2c_data(kSsd1327Addr, gs, sizeof(gs));
}

// ---- SH1107 I2C 128×128 mono ------------------------------------------
//
// Our portable FB is SSD1306/SH1107 *page* layout (MONO_VLSB): 16 pages ×
// 128 columns, 1 byte = 8 vertical pixels.  That matches the MicroPython
// rotate=90 show() path (page addressing + VLSB), so we stay in page mode.
//
// Do NOT software-rotate and do NOT switch to vertical/HMSB — both blanked
// this panel.  Orientation on the AMYboard cutout is already correct.

constexpr uint8_t kSh1107Addr = 0x3c;

// Optional circular column compensate for odd SH1107 modules.  Leave at 0
// for a correctly wired panel — a non-zero value wraps left-edge glyphs
// (e.g. the "1" in centered "120.0") onto the far right of the screen.
constexpr int kXShift = 0;

bool sh1107_init_i2c() {
  // Page addressing + 128×128 flip(False) defaults from sh1107.py (the
  // rotate90 addressing mode, without any content rotation).
  const uint8_t cmds[] = {
      0xAE,        // display off
      0xA8, 0x7F,  // multiplex 128-1
      0x20,        // page addressing (matches VLSB FB)
      0xB0,        // page address 0
      0xAD, 0x81,  // DC-DC enable
      0xD5, 0x50,  // clock
      0xDB, 0x35,  // VCOM
      0xD9, 0x22,  // precharge
      0x81, 0x80,  // contrast
      0xA6,        // normal (not inverted)
      0xD3, 0x00,  // display offset (0 for 128×128)
      0xA0,        // segment re-map
      0xC0,        // scan direction
      0xDC, 0x00,  // start line
      0xA4,        // resume from RAM
      0xAF,        // display on
  };
  for (uint8_t c : cmds) {
    if (!i2c_cmd(kSh1107Addr, c)) {
      return false;
    }
  }
  vTaskDelay(pdMS_TO_TICKS(50));
  ESP_LOGI(kTag, "SH1107 @ 0x%02x (128x128 mono I2C, page mode, xshift=%d)",
           kSh1107Addr, kXShift);
  return true;
}

// Set page + column pointer in one I2C transaction (same as MicroPython
// write_command([B0|page, 0x00|col_lo, 0x10|col_hi])).
bool sh1107_set_page_col(int page, int col) {
  const uint8_t pkt[4] = {
      0x00,  // command control
      static_cast<uint8_t>(0xB0 | (page & 0x0f)),
      static_cast<uint8_t>(0x00 | (col & 0x0f)),
      static_cast<uint8_t>(0x10 | ((col >> 4) & 0x0f)),
  };
  return halesp::i2c_write(kSh1107Addr, pkt, sizeof(pkt));
}

bool sh1107_flush_i2c(const neon::Framebuffer& fb) {
  const uint8_t* src = fb.data();
  uint8_t pagebuf[128];
  for (int page = 0; page < 16; ++page) {
    const uint8_t* row = src + page * 128;
    if (kXShift == 0) {
      std::memcpy(pagebuf, row, 128);
    } else {
      // out[x] = src[(x + kXShift) % 128]  → cancels a +kXShift screen shift
      for (int x = 0; x < 128; ++x) {
        pagebuf[x] = row[(x + kXShift) % 128];
      }
    }
    if (!sh1107_set_page_col(page, 0)) {
      return false;
    }
    if (!i2c_data(kSh1107Addr, pagebuf, 128)) {
      return false;
    }
  }
  return true;
}

// ---- SSD1306 128×64 fallback ------------------------------------------

bool ssd1306_init_i2c() {
  const uint8_t cmds[] = {
      0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
      0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1,
      0xDB, 0x40, 0xA4, 0xA6, 0xAF,
  };
  for (uint8_t c : cmds) {
    if (!i2c_cmd(kSh1107Addr, c)) {
      return false;
    }
  }
  ESP_LOGI(kTag, "SSD1306 @ 0x%02x (128x64 fallback)", kSh1107Addr);
  return true;
}

bool ssd1306_flush_i2c(const neon::Framebuffer& fb) {
  // Only the top 64 rows (pages 0..7).
  const uint8_t* src = fb.data();
  if (!i2c_cmd(kSh1107Addr, 0x21) || !i2c_cmd(kSh1107Addr, 0) ||
      !i2c_cmd(kSh1107Addr, 127) || !i2c_cmd(kSh1107Addr, 0x22) ||
      !i2c_cmd(kSh1107Addr, 0) || !i2c_cmd(kSh1107Addr, 7)) {
    return false;
  }
  return i2c_data(kSh1107Addr, src, 128 * 8);
}

// ---- SPI SH1107 (optional) --------------------------------------------

#if CONFIG_NEON_BOARD_AMYBOARD
spi_device_handle_t g_spi_dev = nullptr;

bool spi_sh1107_cmd(uint8_t cmd) {
  if (kPinDispDc >= 0) {
    gpio_set_level(static_cast<gpio_num_t>(kPinDispDc), 0);
  }
  if (kPinDispCs >= 0) {
    gpio_set_level(static_cast<gpio_num_t>(kPinDispCs), 0);
  }
  spi_transaction_t t = {};
  t.length = 8;
  t.tx_buffer = &cmd;
  const esp_err_t err = spi_device_transmit(g_spi_dev, &t);
  if (kPinDispCs >= 0) {
    gpio_set_level(static_cast<gpio_num_t>(kPinDispCs), 1);
  }
  return err == ESP_OK;
}

bool spi_sh1107_data(const uint8_t* data, size_t len) {
  if (kPinDispDc >= 0) {
    gpio_set_level(static_cast<gpio_num_t>(kPinDispDc), 1);
  }
  if (kPinDispCs >= 0) {
    gpio_set_level(static_cast<gpio_num_t>(kPinDispCs), 0);
  }
  spi_transaction_t t = {};
  t.length = len * 8;
  t.tx_buffer = data;
  const esp_err_t err = spi_device_transmit(g_spi_dev, &t);
  if (kPinDispCs >= 0) {
    gpio_set_level(static_cast<gpio_num_t>(kPinDispCs), 1);
  }
  return err == ESP_OK;
}

bool sh1107_init_spi() {
  if (kPinDispSck < 0 || kPinDispMosi < 0 || kPinDispDc < 0) {
    return false;
  }
  spi_bus_config_t bus = {};
  bus.mosi_io_num = kPinDispMosi;
  bus.miso_io_num = -1;
  bus.sclk_io_num = kPinDispSck;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 128 * 16 + 8;
  // SPI2 may already be claimed; try SPI3 for the display.
  esp_err_t err = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "SPI3 bus init failed: %d", err);
    return false;
  }

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = 10 * 1000 * 1000;
  dev.mode = 0;
  dev.spics_io_num = -1;  // we bit-bang CS so we can interleave DC
  dev.queue_size = 2;
  if (spi_bus_add_device(SPI3_HOST, &dev, &g_spi_dev) != ESP_OK) {
    return false;
  }

  auto cfg_out = [](int pin, int level) {
    if (pin < 0) return;
    gpio_config_t io = {};
    io.pin_bit_mask = 1ull << pin;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level(static_cast<gpio_num_t>(pin), level);
  };
  cfg_out(kPinDispDc, 0);
  cfg_out(kPinDispCs, 1);
  cfg_out(kPinDispRes, 1);
  if (kPinDispRes >= 0) {
    gpio_set_level(static_cast<gpio_num_t>(kPinDispRes), 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(static_cast<gpio_num_t>(kPinDispRes), 1);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  const uint8_t cmds[] = {
      0xAE, 0xDC, 0x00, 0x81, 0x2F, 0x20, 0xA0, 0xC0, 0xA8, 0x7F,
      0xD3, 0x60, 0xD5, 0x51, 0xD9, 0x22, 0xDB, 0x35, 0xA4, 0xA6, 0xAF,
  };
  for (uint8_t c : cmds) {
    if (!spi_sh1107_cmd(c)) {
      return false;
    }
  }
  ESP_LOGI(kTag, "SH1107 SPI 128x128 (SCK=%d MOSI=%d DC=%d CS=%d)",
           kPinDispSck, kPinDispMosi, kPinDispDc, kPinDispCs);
  return true;
}

bool sh1107_flush_spi(const neon::Framebuffer& fb) {
  const uint8_t* src = fb.data();
  for (int page = 0; page < 16; ++page) {
    if (!spi_sh1107_cmd(static_cast<uint8_t>(0xB0 | page)) ||
        !spi_sh1107_cmd(0x00) || !spi_sh1107_cmd(0x10)) {
      return false;
    }
    if (!spi_sh1107_data(src + page * 128, 128)) {
      return false;
    }
  }
  return true;
}
#endif  // AMYBOARD

// ---- probe / public API ------------------------------------------------

bool try_addr(uint8_t addr) { return halesp::i2c_probe(addr, 30); }

}  // namespace

PanelKind panel_init() {
  g_kind = PanelKind::kNone;
  if (halesp::i2c_bus() == nullptr) {
    if (kPinI2cSda >= 0) {
      halesp::i2c_bus_init(kPinI2cSda, kPinI2cScl);
    }
  }

  if (halesp::i2c_bus() != nullptr) {
    // Retry: Grove OLED can miss the first probe right after power-up /
    // while ADS1015+GP8413 are settling on the same front bus.
    for (int attempt = 0; attempt < 4 && g_kind == PanelKind::kNone;
         ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(attempt == 0 ? 50 : 100));

      char seen[80] = {};
      size_t n = 0;
      for (uint8_t a = 0x08; a < 0x78 && n + 4 < sizeof(seen); ++a) {
        if (try_addr(a)) {
          n += static_cast<size_t>(
              snprintf(seen + n, sizeof(seen) - n, "%s%02x", n ? "," : "", a));
        }
      }
      ESP_LOGI(kTag, "I2C scan attempt %d (SDA=%d SCL=%d): %s", attempt + 1,
               kPinI2cSda, kPinI2cScl, n ? seen : "(none)");

      // Prefer the stock amyboard.init_display() order.
      if (try_addr(kSsd1327Addr)) {
        if (ssd1327_init()) {
          g_kind = PanelKind::kSsd1327I2c;
          return g_kind;
        }
        ESP_LOGW(kTag, "SSD1327 @ 0x3d ACKed but init failed");
      }
      if (try_addr(kSh1107Addr)) {
        if (sh1107_init_i2c()) {
          g_kind = PanelKind::kSh1107I2c;
          return g_kind;
        }
        if (ssd1306_init_i2c()) {
          g_kind = PanelKind::kSsd1306I2c;
          return g_kind;
        }
        ESP_LOGW(kTag, "device @ 0x3c ACKed but OLED init failed");
      }
    }
  }

#if CONFIG_NEON_BOARD_AMYBOARD
  if (kPinDispSck >= 0 && kPinDispMosi >= 0 && kPinDispDc >= 0) {
    ESP_LOGI(kTag, "trying SPI SH1107 SCK=%d MOSI=%d CS=%d DC=%d",
             kPinDispSck, kPinDispMosi, kPinDispCs, kPinDispDc);
    if (sh1107_init_spi()) {
      g_kind = PanelKind::kSh1107Spi;
      return g_kind;
    }
  }
#endif

  ESP_LOGW(kTag,
           "no 128x128 panel — need SSD1327@0x3d or SH1107@0x3c on FRONT "
           "Grove I2C (not the back Tulip jack)");
  return PanelKind::kNone;
}

bool panel_flush(const neon::Framebuffer& fb) {
  switch (g_kind) {
    case PanelKind::kSsd1327I2c:
      return ssd1327_flush(fb);
    case PanelKind::kSh1107I2c:
      return sh1107_flush_i2c(fb);
    case PanelKind::kSsd1306I2c:
      return ssd1306_flush_i2c(fb);
#if CONFIG_NEON_BOARD_AMYBOARD
    case PanelKind::kSh1107Spi:
      return sh1107_flush_spi(fb);
#endif
    default:
      return false;
  }
}

PanelKind panel_kind() { return g_kind; }

bool panel_set_brightness(uint8_t level) {
  // Every supported controller takes contrast as 0x81 followed by a byte,
  // so one path covers all of them. Level 0 blanks the panel instead of
  // leaving a dim-but-readable screen, matching what users expect from a
  // brightness control that goes all the way down.
  switch (g_kind) {
    case PanelKind::kSsd1327I2c:
      return i2c_cmd_ctl(kSsd1327Addr, 0x80, 0x81) &&
             i2c_cmd_ctl(kSsd1327Addr, 0x80, level) &&
             i2c_cmd_ctl(kSsd1327Addr, 0x80, level != 0 ? 0xAF : 0xAE);
    case PanelKind::kSh1107I2c:
    case PanelKind::kSsd1306I2c:
      return i2c_cmd(kSh1107Addr, 0x81) && i2c_cmd(kSh1107Addr, level) &&
             i2c_cmd(kSh1107Addr, level != 0 ? 0xAF : 0xAE);
    case PanelKind::kSh1107Spi:
      return spi_sh1107_cmd(0x81) && spi_sh1107_cmd(level) &&
             spi_sh1107_cmd(level != 0 ? 0xAF : 0xAE);
    default:
      return false;
  }
}

}  // namespace oledui
