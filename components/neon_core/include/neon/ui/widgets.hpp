#pragma once

// Reusable draw primitives for the 128×128 panel (DESIGN_SYSTEM.md §13).
//
// Screens compose these; they never touch pixel coordinates directly. That
// is what keeps the panel's proportions editable from design/tokens.json
// and what lets the web UI mirror them.

#include <cstdint>

#include "neon/ui/theme.hpp"

namespace neon::ui {

// ---- text ---------------------------------------------------------------

// Body-font text. `align` is relative to the full panel width when x is
// kAlignPanel, otherwise relative to x.
inline constexpr int kAlignPanel = -1;
void draw_label(Framebuffer& fb, int x, int y, const char* text,
                Align align = Align::kLeft, Font font = kFontBody);

// ---- hero numerals ------------------------------------------------------

int hero_text_width(const char* text);
int draw_hero_text(Framebuffer& fb, int x, int y, const char* text);

// The tempo readout: the one hero element on the live screen. `valid` is
// false before the first Link sync, which renders the "--.-" placeholder
// at the same optical weight rather than a misleading 0.0.
void draw_hero_bpm(Framebuffer& fb, uint32_t milli_bpm, bool valid,
                   int y = kHeroY);

// ---- chrome -------------------------------------------------------------

void draw_icon(Framebuffer& fb, int x, int y, const Icon& icon,
               const IconClocks& clocks = {});

// Brand or screen title on the left, up to `icon_count` status icons packed
// against the right edge, and the rule that closes the header band.
void draw_header(Framebuffer& fb, const char* title,
                 const Icon* const* icons = nullptr, int icon_count = 0,
                 const IconClocks& clocks = {});

// A row of shared status words (LINK / STOP / AP), centred and spaced so
// they read as machine state. Empty entries are skipped.
void draw_status_row(Framebuffer& fb, int y, const char* const* words,
                     int count);

// Phase within the bar, with beat ticks above and below. When the transport
// is stopped the fill is dithered rather than solid, so a held position is
// visibly not advancing.
void draw_bar(Framebuffer& fb, int y, uint32_t phase_milli_beats,
              uint32_t quantum_beats, bool running, int h = kBarH,
              int tick_h = kBarTickH);

// Full-panel beat number for a playing transport. 1-based `beat`.
// White glyph on a black field, 2 px black border, all four beats.
void draw_giant_beat(Framebuffer& fb, uint32_t beat, int height = kHeight);

// Full-panel beat animation. `style` is neon::BeatStyle as a byte
// (widgets stay free of the config header). `phase_milli` is the same
// milli-beat figure the phase bar uses, so the pie, the pendulum and
// the pulse sit on the identical grid as the number.
void draw_beat_stage(Framebuffer& fb, uint32_t phase_milli,
                     uint32_t quantum, uint8_t style, int height = kHeight);

// I2C/SPI page writes scan top-to-bottom. A full-panel beat frame that
// is composed at the audible downbeat still paints its last rows late
// unless the flush starts a couple of milliseconds early. Link phase is
// already correct; this is display transport, not the timeline.
inline constexpr int64_t kBeatFlushLeadUs = 3000;

// True when the next flush should start `kBeatFlushLeadUs` before the
// beat: either ≥30% of pixels change, or the live beat stage is about
// to jump to a new discrete frame (number / pie / pulse). Pendulum is
// continuous, so it only leads when the pixel test fires.
inline bool anticipate_beat_flush(bool beat_stage_jumps, int changed_pixels) {
  return Framebuffer::is_big_redraw(changed_pixels) || beat_stage_jumps;
}

// ---- lists --------------------------------------------------------------

// Inverts a full-width band. Selection has no colour to fall back on, so
// inversion is the panel's focus ring.
void draw_focus(Framebuffer& fb, int y, int h);

// One list row: caret when focused, full inversion while editing so the
// encoder's mode is unambiguous at a glance.
void draw_list_row(Framebuffer& fb, int row, const char* label,
                   const char* value, bool focused, bool editing,
                   int top = kListTop, int row_h = kListRowH);

// ---- confirmation -------------------------------------------------------

// The destructive-action pattern (DESIGN_SYSTEM.md §6). The unselected
// choice is outlined, the selected one inverted.
void draw_confirm(Framebuffer& fb, const char* title, const char* line1,
                  const char* line2, bool yes_selected,
                  const Layout& layout = kLayout128);

}  // namespace neon::ui
