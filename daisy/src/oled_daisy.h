#pragma once

#include <cstdint>

#include "neon/gfx/framebuffer.hpp"

// 128×64 SSD1306/SSD1309 on SPI1, 4-wire mode (D/C on the unused MISO
// pin). The portable Framebuffer is already in SSD1306 page layout, so
// the flush is a straight page stream — the only work is the 128×128 →
// 128×64 question (docs/DAISY.md §4):
//
//   kDownsample (default): OR adjacent pixel rows 2:1 at flush time so
//     the whole UI — hero BPM, status row, identity row, and the phase
//     bar — fits the panel. Text is half height; the large fonts stay
//     legible, kSmall gets marginal. ~20 lines and a 256-byte LUT.
//   kTopHalf: ship rows 0..63 pixel-perfect (hero BPM + header), losing
//     the status row and phase bar. Kept as a build-time escape hatch.
//
// The honest fix — a native 128×64 layout in the design system — is the
// tracked follow-up (docs/DAISY.md §7); it touches the portable
// renderer and the golden fixtures, so it is its own PR.
namespace oled {

enum class DisplayMode : uint8_t { kDownsample, kTopHalf };
inline constexpr DisplayMode kDisplayMode = DisplayMode::kDownsample;

// Init the SPI bus and the controller. Returns false if SPI init fails
// (the UI keeps running headless, driving LEDs only).
bool init();

bool flush(const neon::Framebuffer& fb);

// Maps config display_brightness onto the contrast register (0x81, n);
// 0 blanks the panel entirely (0xAE), matching the other targets.
bool set_brightness(uint8_t level);

}  // namespace oled
