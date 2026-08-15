#pragma once

#include "neon/gfx/framebuffer.hpp"
#include "neon/ui/menu_model.hpp"
#include "neon/ui/theme.hpp"

namespace neon {

// Draws the active screen (SOFTWARE.md §5 OLED requirements: large BPM,
// phase/beat bar, network status, latency, encoder menus). Pure — host
// tests snapshot the framebuffer.
//
// The layout picks the vertical flow: ui::kLayout128 fills the panel,
// ui::kLayout64 renders the compact flow for native 128×64 panels into
// the TOP HALF of the same framebuffer (rows 64..127 stay blank, so a
// 64-row flush is pages 0..7 verbatim). The three-argument form is the
// full 128×128 layout every existing target uses.
void render_ui(const MenuModel& menu, const UiStatus& status,
               Framebuffer& fb, const ui::Layout& layout);

void render_ui(const MenuModel& menu, const UiStatus& status,
               Framebuffer& fb);

}  // namespace neon
