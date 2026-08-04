#pragma once

#include "neon/gfx/framebuffer.hpp"
#include "neon/ui/menu_model.hpp"

namespace neon {

// Draws the active screen (SOFTWARE.md §5 OLED requirements: large BPM,
// phase/beat bar, network status, latency, encoder menus). Pure — host
// tests snapshot the framebuffer.
void render_ui(const MenuModel& menu, const UiStatus& status,
               Framebuffer& fb);

}  // namespace neon
