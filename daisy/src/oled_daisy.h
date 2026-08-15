#pragma once

#include <cstdint>

#include "neon/gfx/framebuffer.hpp"
#include "neon/ui/theme_gen.hpp"

// 128×64 SSD1306/SSD1309 on SPI1, 4-wire mode (D/C on the unused MISO
// pin). The portable Framebuffer is already in SSD1306 page layout, so
// the flush is a straight page stream. Three ways to put a 128-wide UI
// on a 64-row panel (docs/DAISY.md §4):
//
//   kNative (default): render_ui() draws the design system's compact
//     128×64 layout (ui::kLayout64) into the top half of the shared
//     framebuffer — hero BPM, status row, and phase bar purpose-set for
//     this panel, pixel-perfect. The flush is pages 0..7 verbatim.
//   kDownsample: the full 128×128 layout, OR-ing adjacent pixel rows
//     2:1 at flush time. Everything fits but text is half height; kept
//     for anyone who prefers the big layout's information density
//     (identity row, unit label) over crispness.
//   kTopHalf: rows 0..63 of the 128×128 layout, pixel-perfect but
//     missing the status row and phase bar. A bring-up diagnostic.
namespace oled {

enum class DisplayMode : uint8_t { kNative, kDownsample, kTopHalf };
inline constexpr DisplayMode kDisplayMode = DisplayMode::kNative;

// The layout render_ui() should draw for the selected mode.
inline const neon::ui::Layout& layout() {
  return kDisplayMode == DisplayMode::kNative ? neon::ui::kLayout64
                                              : neon::ui::kLayout128;
}

// Init the SPI bus and the controller. Returns false if SPI init fails
// (the UI keeps running headless, driving LEDs only).
bool init();

bool flush(const neon::Framebuffer& fb);

// Maps config display_brightness onto the contrast register (0x81, n);
// 0 blanks the panel entirely (0xAE), matching the other targets.
bool set_brightness(uint8_t level);

}  // namespace oled
