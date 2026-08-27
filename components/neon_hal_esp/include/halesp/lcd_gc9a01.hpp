#pragma once

#include <cstdint>

#include "sdkconfig.h"

// GC9A01 240×240 round IPS panel over SPI. Used by the MaTouch ESP32-S3
// 1.28" Rotary target (docs/LINKSYNC_MATOUCH.md). Thin wrapper over the
// esp_lcd SPI panel driver + the espressif/esp_lcd_gc9a01 vendor init.
//
// The renderer builds a full 240×240 RGB565 frame and blits it in one
// draw_bitmap. Bytes are stored big-endian in the framebuffer (see
// kGc9a01SwapBytes) so they clock out in the order GC9A01 expects.

namespace halesp {

constexpr int kGc9a01W = 240;
constexpr int kGc9a01H = 240;
// GC9A01 wants each RGB565 pixel MSB-first on the wire; the S3 is
// little-endian, so the framebuffer pre-swaps the two bytes.
constexpr bool kGc9a01SwapBytes = true;

// Bring up SPI bus, panel, and backlight. Idempotent-ish: safe to call
// once from the display task. Returns false if the panel refused to init.
bool lcd_gc9a01_init();

// Backlight on/off (GPIO push, active-high per the MaTouch schematic).
void lcd_gc9a01_backlight(bool on);

// Blit a full-frame RGB565 buffer (already byte-swapped) to the panel.
bool lcd_gc9a01_blit(const uint16_t* rgb565, int x, int y, int w, int h);

}  // namespace halesp
