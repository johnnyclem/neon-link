#include "oled_daisy.h"

#include "daisy_seed.h"

#include "board_pins_daisy.h"

namespace oled {
namespace {

daisy::SpiHandle g_spi;
daisy::GPIO g_cs;
daisy::GPIO g_dc;
daisy::GPIO g_rst;
bool g_ready = false;

// OR-pairs-of-bits LUT for the 2:1 vertical downsample: 8 source bits
// (one FB byte = 8 vertical pixels) → 4 output bits.
uint8_t g_pair_lut[256];

bool spi_write(const uint8_t* data, size_t len, bool is_data) {
  g_dc.Write(is_data);
  g_cs.Write(false);
  const daisy::SpiHandle::Result r = g_spi.BlockingTransmit(
      const_cast<uint8_t*>(data), len, /*timeout=*/100);
  g_cs.Write(true);
  return r == daisy::SpiHandle::Result::OK;
}

bool cmd(uint8_t c) { return spi_write(&c, 1, false); }

bool cmds(const uint8_t* c, size_t n) { return spi_write(c, n, false); }

}  // namespace

bool init() {
  for (int i = 0; i < 256; ++i) {
    uint8_t out = 0;
    for (int b = 0; b < 4; ++b) {
      if ((i >> (2 * b)) & 3) {
        out |= 1u << b;
      }
    }
    g_pair_lut[i] = out;
  }

  g_cs.Init(kPinOledCs, daisy::GPIO::Mode::OUTPUT);
  g_cs.Write(true);
  g_dc.Init(kPinOledDc, daisy::GPIO::Mode::OUTPUT);
  g_rst.Init(kPinOledRst, daisy::GPIO::Mode::OUTPUT);

  daisy::SpiHandle::Config cfg;
  cfg.periph = daisy::SpiHandle::Config::Peripheral::SPI_1;
  cfg.mode = daisy::SpiHandle::Config::Mode::MASTER;
  cfg.direction = daisy::SpiHandle::Config::Direction::TWO_LINES_TX_ONLY;
  cfg.nss = daisy::SpiHandle::Config::NSS::SOFT;
  cfg.pin_config.sclk = kPinOledSck;
  cfg.pin_config.mosi = kPinOledMosi;
  // ~6 MHz from the 100 MHz SPI1 kernel clock — inside the SSD1306's
  // 10 MHz limit; a full 1 KB frame is ~1.4 ms.
  cfg.baud_prescaler = daisy::SpiHandle::Config::BaudPrescaler::PS_16;
  if (g_spi.Init(cfg) != daisy::SpiHandle::Result::OK) {
    return false;
  }

  // Hardware reset pulse.
  g_rst.Write(true);
  daisy::System::Delay(1);
  g_rst.Write(false);
  daisy::System::Delay(10);
  g_rst.Write(true);
  daisy::System::Delay(10);

  // 128×64 init, horizontal addressing (matches the FB page layout).
  // SSD1309 is command-compatible; external-VCC modules skip the
  // SSD1306 charge pump.
  static const uint8_t kInit[] = {
      0xAE,        // display off
      0xD5, 0x80,  // clock divide
      0xA8, 0x3F,  // multiplex 64-1
      0xD3, 0x00,  // display offset
      0x40,        // start line 0
      0x20, 0x00,  // horizontal addressing
      0xA1,        // segment remap
      0xC8,        // COM scan direction (flip both = 180° mount option)
      0xDA, 0x12,  // COM pins: alternative, no remap
      0x81, 0xCF,  // contrast
      0xD9, 0xF1,  // precharge
      0xDB, 0x40,  // VCOMH
      0xA4,        // resume from RAM
      0xA6,        // normal (not inverted)
  };
  if (!cmds(kInit, sizeof(kInit))) {
    return false;
  }
  if (!kOledExternalVcc) {
    static const uint8_t kChargePump[] = {0x8D, 0x14};
    if (!cmds(kChargePump, sizeof(kChargePump))) {
      return false;
    }
  }
  if (!cmd(0xAF)) {  // display on
    return false;
  }
  g_ready = true;
  return true;
}

bool flush(const neon::Framebuffer& fb) {
  if (!g_ready) {
    return false;
  }
  // Full-window address reset each frame (simple + reliable, like the
  // ESP panel drivers).
  static const uint8_t kWindow[] = {0x21, 0, 127, 0x22, 0, 7};
  if (!cmds(kWindow, sizeof(kWindow))) {
    return false;
  }

  const uint8_t* src = fb.data();
  uint8_t out[128 * 8];
  if (kDisplayMode != DisplayMode::kDownsample) {
    // kNative: the compact layout only ever draws in the top half, so FB
    // pages 0..7 ARE the frame (the host suite asserts the invariant).
    // kTopHalf: same bytes, cropping the 128×128 layout instead.
    for (int i = 0; i < 128 * 8; ++i) {
      out[i] = src[i];
    }
  } else {
    // 2:1 vertical downsample: panel page p rows come from FB pages
    // 2p/2p+1; OR-ing pixel-row pairs keeps 1-px strokes visible.
    for (int page = 0; page < 8; ++page) {
      const uint8_t* lo = src + (2 * page) * 128;
      const uint8_t* hi = src + (2 * page + 1) * 128;
      uint8_t* dst = out + page * 128;
      for (int x = 0; x < 128; ++x) {
        dst[x] = static_cast<uint8_t>(g_pair_lut[lo[x]] |
                                      (g_pair_lut[hi[x]] << 4));
      }
    }
  }
  return spi_write(out, sizeof(out), true);
}

bool set_brightness(uint8_t level) {
  if (!g_ready) {
    return false;
  }
  const uint8_t seq[] = {0x81, level,
                         static_cast<uint8_t>(level != 0 ? 0xAF : 0xAE)};
  return cmds(seq, sizeof(seq));
}

}  // namespace oled
