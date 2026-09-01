#pragma once

#include <cstdint>

#include "sdkconfig.h"

// AXS15231B 320×480 IPS panel over QSPI + AXS15231B cap-touch over I2C.
// Used by the Guition JC3248W535 target (docs/LINKSYNC_JC3248.md). Thin
// wrapper over the espressif/esp_lcd_axs15231b managed component: the
// panel is created with QSPI framing and the board's vendor init array
// (ported from the working JC3248W535EN demo), the touch with the
// component's I2C driver at address 0x3B.
//
// The renderer builds a full 320×480 RGB565 frame in PSRAM and blits it
// band-by-band through an internal DMA bounce buffer, exactly like the
// GC9A01 path. Bytes are stored big-endian in the framebuffer (see
// kAxsSwapBytes) so they clock out in the order the panel expects.

namespace halesp {

constexpr int kAxsW = 320;   // native portrait width
constexpr int kAxsH = 480;   // native portrait height
// The panel takes RGB565 MSB-first on the wire; the S3 is little-endian,
// so the framebuffer pre-swaps the two bytes.
constexpr bool kAxsSwapBytes = true;

// Bring up the QSPI bus, panel (with the board init array), backlight
// pin, and DMA bounce buffer. Panel is left display-OFF until the first
// blit so no GRAM garbage shows. Returns false if the panel refused.
bool lcd_axs15231b_init();

// Backlight on/off (LEDC PWM on the backlight pin, active HIGH).
void lcd_axs15231b_backlight(bool on);

// Graded backlight, 0..255, via 20 kHz LEDC PWM. 0 is fully dark.
void lcd_axs15231b_backlight_level(uint8_t level);

// Blit a full-frame RGB565 buffer (already byte-swapped) to the panel.
// The QSPI panel streams top-to-bottom, so callers must blit the whole
// frame starting at row 0. Turns the display on after the first frame.
bool lcd_axs15231b_blit(const uint16_t* rgb565, int x, int y, int w, int h);

// Bring up the AXS15231B cap-touch on I2C (polled; no INT/RST wired).
// Safe to call once from the display task. Returns false on failure.
bool lcd_axs15231b_touch_init(int sda, int scl);

// Poll the touch controller. Returns true while a finger is down, with
// (*x,*y) in panel pixels (0..kAxsW-1, 0..kAxsH-1).
bool lcd_axs15231b_touch_poll(int* x, int* y);

}  // namespace halesp
