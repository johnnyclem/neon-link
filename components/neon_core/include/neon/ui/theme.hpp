#pragma once

// The device half of the NEON LINK design system.
//
// Everything a screen needs in order to place something on the 128×128
// panel lives here or in the generated headers this pulls in. Screens must
// not invent pixel offsets, spell their own status words, or pick a font
// by scale factor — if a value is missing, it belongs in design/tokens.json
// and comes back through scripts/gen_design.py.
//
//   theme_gen.hpp      layout constants + the shared status vocabulary
//   icons_gen.hpp      the 8×8 1-bit icon masters
//   hero_font_gen.hpp  the seven-segment tempo numerals

#include "neon/gfx/framebuffer.hpp"
#include "neon/ui/hero_font_gen.hpp"
#include "neon/ui/icons_gen.hpp"
#include "neon/ui/theme_gen.hpp"

namespace neon::ui {

// The type ladder, named by role rather than by scale so screens read as
// intent. DESIGN_SYSTEM.md §4.
//   kHero   seven-segment tempo numerals (26px) — the one hero element
//   kValue  10×14 — a value that needs to be read from across a room
//   kBody   5×7 — labels, list rows, status words
using Font = Framebuffer::Font;
inline constexpr Font kFontValue = Font::kMedium;
inline constexpr Font kFontBody = Font::kSmall;

using Dither = Framebuffer::Dither;

enum class Align : uint8_t { kLeft, kCenter, kRight };

// Advance width of one body-font row of text, for right-aligning values.
inline int body_width(const char* s) {
  return Framebuffer::text_width(s, kFontBody);
}

}  // namespace neon::ui
