#include <doctest.h>

#include <cstring>
#include <string>

#include "neon/gfx/framebuffer.hpp"
#include "neon/ui/menu_model.hpp"
#include "neon/ui/render.hpp"

namespace {

std::string dump(const neon::Framebuffer& fb) {
  std::string out;
  char row[neon::Framebuffer::kWidth + 1];
  for (int y = 0; y < neon::Framebuffer::kHeight; ++y) {
    fb.ascii_row(y, row);
    out += row;
    out += '\n';
  }
  return out;
}

int lit_pixels(const neon::Framebuffer& fb) {
  int n = 0;
  for (int y = 0; y < neon::Framebuffer::kHeight; ++y) {
    for (int x = 0; x < neon::Framebuffer::kWidth; ++x) {
      if (fb.pixel(x, y)) ++n;
    }
  }
  return n;
}

}  // namespace

TEST_CASE("framebuffer pixel ops and page layout") {
  neon::Framebuffer fb;
  fb.clear();
  CHECK(lit_pixels(fb) == 0);

  fb.set_pixel(0, 0, true);
  fb.set_pixel(127, 127, true);
  fb.set_pixel(-1, 0, true);    // out of range: ignored
  fb.set_pixel(0, 128, true);   // out of range: ignored
  CHECK(fb.pixel(0, 0));
  CHECK(fb.pixel(127, 127));
  CHECK(lit_pixels(fb) == 2);

  // Page layout: (0,0) is bit 0 of byte 0; (127,127) is bit 7 of the last
  // page's last column.
  CHECK((fb.data()[0] & 0x01) != 0);
  CHECK((fb.data()[neon::Framebuffer::kSize - 1] & 0x80) != 0);

  fb.fill_rect(10, 10, 4, 4, true);
  CHECK(lit_pixels(fb) == 2 + 16);
  fb.fill_rect(10, 10, 4, 4, false);
  CHECK(lit_pixels(fb) == 2);
}

TEST_CASE("glyph rendering: known pattern for '1'") {
  neon::Framebuffer fb;
  fb.draw_text(0, 0, "1", neon::Framebuffer::Font::kSmall);
  // '1' column pattern {0x00,0x42,0x7f,0x40,0x00}: column 2 is a full
  // 7-pixel bar.
  for (int y = 0; y < 7; ++y) {
    CHECK(fb.pixel(2, y));
  }
  CHECK_FALSE(fb.pixel(0, 0));
  CHECK(fb.pixel(1, 1));  // 0x42: bit1
  CHECK(fb.pixel(1, 6));  // 0x42: bit6
}

TEST_CASE("font scaling multiplies glyph size") {
  neon::Framebuffer small;
  small.draw_text(0, 0, "8", neon::Framebuffer::Font::kSmall);
  neon::Framebuffer large;
  large.draw_text(0, 0, "8", neon::Framebuffer::Font::kLarge);
  CHECK(lit_pixels(large) == 9 * lit_pixels(small));
}

TEST_CASE("home screen renders deterministically (golden)") {
  neon::Config cfg;
  neon::MenuModel menu(&cfg);
  neon::UiStatus st;
  st.milli_bpm = 120000;
  st.peers = 2;
  st.playing = true;
  st.active_net = 1;
  st.phase_milli_beats = 2000;  // half way through a 4-beat bar
  st.quantum_beats = 4;

  neon::Framebuffer a;
  neon::Framebuffer b;
  neon::render_ui(menu, st, a);
  neon::render_ui(menu, st, b);
  CHECK(dump(a) == dump(b));
  CHECK(lit_pixels(a) > 100);

  // Phase bar interior (y≈108): fill reaches about half width at phase 2/4.
  int fill_end = 0;
  for (int x = 3; x < 126; ++x) {
    if (a.pixel(x, 108)) fill_end = x;
  }
  CHECK(fill_end > 55);
  CHECK(fill_end < 70);

  // Different phase changes the render.
  st.phase_milli_beats = 3900;
  neon::Framebuffer c;
  neon::render_ui(menu, st, c);
  CHECK(dump(a) != dump(c));
}

TEST_CASE("menu navigation walks the screen graph") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  using S = neon::MenuModel::Screen;

  CHECK(m.screen() == S::kHome);
  m.on_rotate(3);  // rotation on home does nothing
  CHECK(m.screen() == S::kHome);

  m.on_click();
  CHECK(m.screen() == S::kMenu);
  CHECK(m.cursor() == 0);

  m.on_click();  // Outputs
  CHECK(m.screen() == S::kOutputs);
  m.on_rotate(2);
  CHECK(m.cursor() == 2);
  m.on_click();  // CLK 3
  CHECK(m.screen() == S::kOutputEdit);
  CHECK(m.output_index() == 2);

  // Back is the last item.
  m.on_rotate(neon::MenuModel::kOutputEditItems - 1);
  m.on_click();
  CHECK(m.screen() == S::kOutputs);

  m.on_rotate(2);  // to Back (cursor was 2 -> 4)
  m.on_click();
  CHECK(m.screen() == S::kMenu);
  m.on_rotate(1);
  m.on_click();  // Settings
  CHECK(m.screen() == S::kSettings);

  // Wrap-around navigation.
  m.on_rotate(-1);
  CHECK(m.cursor() == neon::MenuModel::kSettingsItems - 1);
  m.on_click();  // Back
  CHECK(m.screen() == S::kMenu);
  m.on_rotate(1);
  m.on_click();
  CHECK(m.screen() == S::kHome);
}

TEST_CASE("editing mutates config with clamping and marks dirty") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  m.on_click();      // Menu
  m.on_click();      // Outputs
  m.on_click();      // CLK 1 edit
  m.on_rotate(1);    // cursor -> PPQN
  CHECK_FALSE(m.take_dirty());
  m.on_click();      // enter edit
  CHECK(m.editing());
  m.on_rotate(4);    // 4 -> 8
  CHECK(cfg.engine.clocks[0].ppqn == 8);
  CHECK(m.take_dirty());
  CHECK_FALSE(m.take_dirty());  // one-shot
  m.on_rotate(1000);
  CHECK(cfg.engine.clocks[0].ppqn == 192);  // clamped
  m.on_click();      // leave edit
  CHECK_FALSE(m.editing());
  // Rotation now moves the cursor again, not the value.
  m.on_rotate(1);
  CHECK(cfg.engine.clocks[0].ppqn == 192);
  CHECK(m.cursor() == 2);
}

TEST_CASE("settings edit covers latency and enums") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  m.on_click();    // Menu
  m.on_rotate(1);  // Settings
  m.on_click();
  m.on_click();    // edit latency
  m.on_rotate(-5);
  CHECK(cfg.engine.latency_us == -500);
  m.on_click();    // leave edit
  m.on_rotate(1);  // RESET mode
  m.on_click();
  m.on_rotate(1);
  CHECK(cfg.engine.reset_mode == neon::ResetMode::kEveryBar);
  m.on_click();
  m.on_rotate(1);  // SOURCE
  m.on_click();
  m.on_rotate(1);
  CHECK(cfg.clock_source == neon::ClockSource::kLinkMaster);
  CHECK(m.take_dirty());
}
