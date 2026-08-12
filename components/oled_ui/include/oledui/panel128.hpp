#pragma once

#include <cstddef>
#include <cstdint>

#include "neon/gfx/framebuffer.hpp"

namespace oledui {

// Detected panel flavour after probe.
enum class PanelKind : uint8_t {
  kNone = 0,
  kSsd1327I2c,   // Adafruit 1.5" 128×128 grayscale (addr 0x3d)
  kSh1107I2c,    // Generic 128×128 mono (addr 0x3c)
  kSsd1306I2c,   // Classic 128×64 mono (addr 0x3c) — top half only
  kSh1107Spi,    // 128×128 mono over SPI (AMYboard SPI0 + DC/CS)
};

// Probe and initialize the best available 128×128 panel. Returns kind
// (kNone if nothing answered). Safe to call once at boot on the shared
// I2C bus; SPI is only attempted when pins are configured (>= 0).
PanelKind panel_init();

// Push the monochrome framebuffer to the active panel. No-op if none.
bool panel_flush(const neon::Framebuffer& fb);

PanelKind panel_kind();

// Set panel contrast, 0..255. 0 blanks the display entirely. No-op (and
// false) when no panel was detected.
bool panel_set_brightness(uint8_t level);

}  // namespace oledui
