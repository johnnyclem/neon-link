#pragma once

#include <cstddef>
#include <cstdint>

#include "sdkconfig.h"

namespace halesp {

#if CONFIG_NEON_BOARD_LINKSYNC_TAB5
// M5Stack Tab5 native portrait MIPI-DSI panel.
constexpr int kLcdW = 720;
constexpr int kLcdH = 1280;
constexpr bool kLcdSwapBytes = false;
#else
// CrowPanel Advance 5.0" RGB565 panel (800×480).
constexpr int kLcdW = 800;
constexpr int kLcdH = 480;
constexpr bool kLcdSwapBytes = true;
#endif

// LDO3=2.5 V / LDO4=3.3 V. Call before LCD *and* before talking to the C6
// — the radio module is on the 3.3 V rail. Idempotent.
bool lcd_rgb_ldos();
bool lcd_rgb_init();
void lcd_rgb_backlight(uint8_t duty);
// Draw a full-screen RGB565 buffer (native panel endian, already swapped).
bool lcd_rgb_blit(const uint16_t* rgb565, int x, int y, int w, int h);

// Double-buffered scanout (RGB panel only). next_frame() hands out the
// back buffer to compose the next full frame into; present() queues the
// flip and blocks until the frame that was in flight has fully left the
// panel, so the buffer the following next_frame() returns is no longer
// being scanned. nullptr / false where the panel has no internal double
// buffer (Tab5 DSI) — callers fall back to an own buffer + lcd_rgb_blit,
// which copies into scanout mid-refresh and can tear.
uint16_t* lcd_rgb_next_frame();
bool lcd_rgb_present();

// Capacitive panel. Tab5: ST7123 @ 0x55 (GT911 fallback).
// CrowPanel Advance 5.0": GT911 on I2C 45/46, RST 36, INT 42.
bool lcd_touch_init();
bool lcd_touch_ok();
// Latest sample in panel pixels. True while a finger is down.
bool lcd_touch_poll(int* x, int* y);

}  // namespace halesp
