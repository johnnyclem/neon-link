#include <doctest.h>

#include <cstring>
#include <string>

#include "neon/gfx/framebuffer.hpp"
#include "neon/ui/menu_model.hpp"
#include "neon/ui/render.hpp"
#include "neon/ui/widgets.hpp"

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

int lit_in_row(const neon::Framebuffer& fb, int y) {
  int n = 0;
  for (int x = 0; x < neon::Framebuffer::kWidth; ++x) {
    if (fb.pixel(x, y)) ++n;
  }
  return n;
}

// Renders the live screen for a status, which is what most of the golden
// checks below need.
std::string render_home(const neon::UiStatus& st) {
  neon::Config cfg;
  neon::MenuModel menu(&cfg);
  neon::Framebuffer fb;
  neon::render_ui(menu, st, fb);
  return dump(fb);
}

neon::UiStatus playing_status() {
  neon::UiStatus st;
  st.milli_bpm = 120000;
  st.peers = 2;
  st.playing = true;
  st.active_net = 1;
  st.phase_milli_beats = 2000;  // half way through a 4-beat bar
  st.quantum_beats = 4;
  // Existing layout checks cover the classic home; giant beats are
  // tested separately so one flag flip does not rewrite every fixture.
  st.big_beat_display = false;
  return st;
}

}  // namespace

// ---------------------------------------------------------------------------
// framebuffer primitives
// ---------------------------------------------------------------------------

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

TEST_CASE("dither patterns light the documented fraction of a region") {
  using D = neon::Framebuffer::Dither;

  neon::Framebuffer solid;
  solid.fill_rect_dither(0, 0, 8, 8, D::kSolid);
  CHECK(lit_pixels(solid) == 64);

  neon::Framebuffer half;
  half.fill_rect_dither(0, 0, 8, 8, D::kHalf);
  CHECK(lit_pixels(half) == 32);
  CHECK(half.pixel(0, 0));
  CHECK_FALSE(half.pixel(1, 0));
  CHECK(half.pixel(1, 1));

  neon::Framebuffer quarter;
  quarter.fill_rect_dither(0, 0, 8, 8, D::kQuarter);
  CHECK(lit_pixels(quarter) == 16);
  CHECK(quarter.pixel(0, 0));
  CHECK_FALSE(quarter.pixel(1, 0));
  CHECK_FALSE(quarter.pixel(0, 1));

  // Dither only ever adds ink; it never clears what is already there.
  neon::Framebuffer over;
  over.fill_rect(0, 0, 8, 8, true);
  over.fill_rect_dither(0, 0, 8, 8, D::kQuarter);
  CHECK(lit_pixels(over) == 64);
}

TEST_CASE("invert_rect flips only the region and clips at the edges") {
  neon::Framebuffer fb;
  fb.fill_rect(0, 0, 4, 4, true);
  fb.invert_rect(0, 0, 8, 4);
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      CHECK_FALSE(fb.pixel(x, y));
    }
    for (int x = 4; x < 8; ++x) {
      CHECK(fb.pixel(x, y));
    }
  }

  // Straddling the edge must not corrupt the opposite side of the buffer.
  neon::Framebuffer edge;
  edge.invert_rect(126, 0, 8, 1);
  CHECK(edge.pixel(126, 0));
  CHECK(edge.pixel(127, 0));
  CHECK(lit_in_row(edge, 0) == 2);
}

TEST_CASE("blit renders a 1-bit master MSB-first") {
  // A single lit pixel in the top-left of an 8x8 master.
  const uint8_t rows[8] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  neon::Framebuffer fb;
  fb.blit(10, 20, rows, 8, 8);
  CHECK(fb.pixel(10, 20));       // bit 7 is the leftmost column
  CHECK_FALSE(fb.pixel(11, 20));
  CHECK(fb.pixel(17, 27));       // bit 0 is the rightmost column
  CHECK(lit_pixels(fb) == 2);
}

// ---------------------------------------------------------------------------
// fonts
// ---------------------------------------------------------------------------

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

TEST_CASE("hero numerals are tabular and segment-shaped") {
  using namespace neon::ui;

  // Tabular figures: every digit advances by the same width, so a tempo
  // readout never jitters as the value changes (DESIGN_SYSTEM.md §4).
  CHECK(hero_text_width("1") == hero_text_width("8"));
  CHECK(hero_text_width("000") == hero_text_width("888"));

  // The decimal point is narrower than a digit, and tracking is added
  // between glyphs but not before the first one.
  CHECK(hero_text_width(".") < hero_text_width("0"));
  CHECK(hero_text_width("00") ==
        2 * hero_text_width("0") + kHeroTracking);

  // "120.0" has to fit the panel with room to spare.
  CHECK(hero_text_width("120.0") < kWidth - 2 * kMargin);

  // '8' lights every segment, so it is the densest glyph; ' ' is empty.
  neon::Framebuffer eight;
  draw_hero_text(eight, 0, 0, "8");
  neon::Framebuffer one;
  draw_hero_text(one, 0, 0, "1");
  neon::Framebuffer blank;
  draw_hero_text(blank, 0, 0, " ");
  CHECK(lit_pixels(eight) > lit_pixels(one));
  CHECK(lit_pixels(blank) == 0);

  // '1' is segments B and C: the right-hand bar, full height, nothing left.
  CHECK(one.pixel(kHeroMaxWidth - 1, 0));
  CHECK(one.pixel(kHeroMaxWidth - 1, kHeroHeight - 1));
  CHECK_FALSE(one.pixel(0, kHeroHeight / 2));

  // An unsupported character is skipped rather than drawn as a blank box.
  CHECK(hero_text_width("Z") == 0);
}

// ---------------------------------------------------------------------------
// live screen
// ---------------------------------------------------------------------------

TEST_CASE("home screen renders deterministically (golden)") {
  const neon::UiStatus st = playing_status();
  const std::string a = render_home(st);
  CHECK(a == render_home(st));

  neon::Config cfg;
  neon::MenuModel menu(&cfg);
  neon::Framebuffer fb;
  neon::render_ui(menu, st, fb);
  CHECK(lit_pixels(fb) > 100);

  // Header rule spans the full width and closes the header band.
  CHECK(lit_in_row(fb, neon::ui::kHeaderRuleY) == neon::Framebuffer::kWidth);

  // The hero readout occupies its band and is the densest thing on screen.
  int hero_ink = 0;
  for (int y = neon::ui::kHeroY;
       y < neon::ui::kHeroY + neon::ui::kHeroHeight; ++y) {
    hero_ink += lit_in_row(fb, y);
  }
  CHECK(hero_ink > 300);

  // Phase bar interior: fill reaches about half width at phase 2/4.
  const int bar_mid = neon::ui::kBarY + neon::ui::kBarH / 2;
  int fill_end = 0;
  for (int x = neon::ui::kBarInset; x < 126; ++x) {
    if (fb.pixel(x, bar_mid)) fill_end = x;
  }
  CHECK(fill_end > 55);
  CHECK(fill_end < 70);

  // Different phase changes the render.
  neon::UiStatus later = st;
  later.phase_milli_beats = 3900;
  CHECK(render_home(later) != a);
}

TEST_CASE("hero shows a placeholder until the first sync") {
  neon::UiStatus st = playing_status();
  st.tempo_valid = false;
  const std::string placeholder = render_home(st);
  CHECK(placeholder != render_home(playing_status()));

  // "--.-" is drawn from the middle segment only, so the hero band has ink
  // but far less of it than a real tempo.
  neon::Config cfg;
  neon::MenuModel menu(&cfg);
  neon::Framebuffer fb;
  neon::render_ui(menu, st, fb);
  int hero_ink = 0;
  for (int y = neon::ui::kHeroY;
       y < neon::ui::kHeroY + neon::ui::kHeroHeight; ++y) {
    hero_ink += lit_in_row(fb, y);
  }
  CHECK(hero_ink > 0);
  CHECK(hero_ink < 300);
}

TEST_CASE("a stopped transport dithers the phase fill") {
  neon::UiStatus running = playing_status();
  neon::UiStatus stopped = playing_status();
  stopped.playing = false;

  neon::Config cfg;
  neon::MenuModel menu(&cfg);
  neon::Framebuffer run_fb;
  neon::Framebuffer stop_fb;
  neon::render_ui(menu, running, run_fb);
  neon::render_ui(menu, stopped, stop_fb);

  const int mid = neon::ui::kBarY + neon::ui::kBarH / 2;
  const int run_ink = lit_in_row(run_fb, mid);
  const int stop_ink = lit_in_row(stop_fb, mid);
  CHECK(run_ink > 0);
  // Half-tone fill: roughly half the ink of the solid one on any given row.
  CHECK(stop_ink < run_ink);
  CHECK(stop_ink > run_ink / 4);
}

TEST_CASE("the live screen falls back to the setup address in AP mode") {
  neon::UiStatus ap;
  ap.setup_ap = true;
  ap.active_net = 0;
  const std::string with_ap = render_home(ap);

  neon::UiStatus joined = ap;
  joined.setup_ap = false;
  joined.active_net = 2;
  std::snprintf(joined.ip, sizeof(joined.ip), "10.0.0.7");
  CHECK(with_ap != render_home(joined));
}

// ---------------------------------------------------------------------------
// navigation
// ---------------------------------------------------------------------------

TEST_CASE("menu navigation walks the shallow screen graph") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  using S = neon::MenuModel::Screen;

  CHECK(m.screen() == S::kHome);
  m.on_rotate(3);  // rotation on home does nothing
  CHECK(m.screen() == S::kHome);

  m.on_click();
  CHECK(m.screen() == S::kMenu);
  CHECK(m.cursor() == 0);

  // Item 0 is LIVE — the way back to the live screen without a long press.
  m.on_click();
  CHECK(m.screen() == S::kHome);

  m.on_click();
  m.on_rotate(1);  // OUTPUTS
  m.on_click();
  CHECK(m.screen() == S::kOutputs);
  m.on_rotate(2);
  CHECK(m.cursor() == 2);
  m.on_click();  // CLK 3
  CHECK(m.screen() == S::kOutputEdit);
  CHECK(m.output_index() == 2);

  // Long press is the universal way back, and it restores the cursor to
  // the item you came from.
  m.on_long_press();
  CHECK(m.screen() == S::kOutputs);
  CHECK(m.cursor() == 2);
  m.on_long_press();
  CHECK(m.screen() == S::kMenu);
  CHECK(m.cursor() == 1);
  m.on_long_press();
  CHECK(m.screen() == S::kHome);
  m.on_long_press();  // already home: no-op, never wraps into a menu
  CHECK(m.screen() == S::kHome);

  // Every menu destination is reachable and reports its own title.
  const S expected[5] = {S::kHome, S::kOutputs, S::kNetwork, S::kMidi,
                         S::kSystem};
  for (int i = 0; i < 5; ++i) {
    neon::MenuModel nav(&cfg);
    nav.on_click();
    nav.on_rotate(i);
    nav.on_click();
    CHECK(nav.screen() == expected[i]);
  }

  // Cursor wraps in both directions.
  neon::MenuModel wrap(&cfg);
  wrap.on_click();
  wrap.on_rotate(-1);
  CHECK(wrap.cursor() == neon::MenuModel::kMenuItems - 1);
  wrap.on_rotate(1);
  CHECK(wrap.cursor() == 0);
}

TEST_CASE("long press cancels an edit before it leaves the screen") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  using S = neon::MenuModel::Screen;

  m.on_click();
  m.on_rotate(1);
  m.on_click();  // Outputs
  m.on_click();  // CLK 1 edit
  m.on_rotate(1);
  m.on_click();  // enter edit on PPQN
  CHECK(m.editing());

  m.on_long_press();
  CHECK_FALSE(m.editing());
  CHECK(m.screen() == S::kOutputEdit);  // still here — the edit was the escape

  m.on_long_press();
  CHECK(m.screen() == S::kOutputs);
}

TEST_CASE("reboot goes through a confirmation screen") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  using S = neon::MenuModel::Screen;
  using A = neon::MenuModel::Action;

  auto to_reboot = [&]() {
    m.on_click();                                    // Menu
    m.on_rotate(4);                                  // System
    m.on_click();
    m.on_rotate(neon::MenuModel::kSystemRebootItem);  // REBOOT
    m.on_click();
    CHECK(m.screen() == S::kConfirm);
  };

  // Defaults to NO, and confirming NO returns to the list without acting.
  to_reboot();
  CHECK_FALSE(m.confirm_yes());
  m.on_click();
  CHECK(m.screen() == S::kSystem);
  CHECK(m.take_action() == A::kNone);

  // Rotating selects YES; clicking raises the action exactly once.
  m.on_click();
  CHECK(m.screen() == S::kConfirm);
  m.on_rotate(1);
  CHECK(m.confirm_yes());
  m.on_click();
  CHECK(m.screen() == S::kHome);
  CHECK(m.take_action() == A::kReboot);
  CHECK(m.take_action() == A::kNone);  // one-shot

  // Long press abandons the confirmation.
  m.on_click();
  m.on_rotate(4);
  m.on_click();
  m.on_rotate(neon::MenuModel::kSystemRebootItem);
  m.on_click();
  m.on_long_press();
  CHECK(m.screen() == S::kSystem);
  CHECK(m.take_action() == A::kNone);
}

// ---------------------------------------------------------------------------
// editing
// ---------------------------------------------------------------------------

TEST_CASE("editing mutates config with clamping and marks dirty") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  m.on_click();      // Menu
  m.on_rotate(1);    // Outputs
  m.on_click();
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

TEST_CASE("system edit covers latency, enums and quantum") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  m.on_click();    // Menu
  m.on_rotate(4);  // System
  m.on_click();

  m.on_click();    // edit latency
  m.on_rotate(-5);
  CHECK(cfg.engine.latency_us == -500);
  m.on_click();

  m.on_rotate(1);  // RESET mode
  m.on_click();
  m.on_rotate(1);
  CHECK(cfg.engine.reset_mode == neon::ResetMode::kEveryBar);
  m.on_click();

  m.on_rotate(1);  // SOURCE
  m.on_click();
  m.on_rotate(1);
  CHECK(cfg.clock_source == neon::ClockSource::kLinkMaster);
  m.on_click();

  m.on_rotate(3);  // QUANTUM
  m.on_click();
  m.on_rotate(-100);
  CHECK(cfg.quantum_beats == 1);  // clamped, not wrapped
  CHECK(m.take_dirty());
}

TEST_CASE("midi edit wraps channel and gate target through their off state") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  m.on_click();    // Menu
  m.on_rotate(3);  // MIDI
  m.on_click();

  m.on_click();    // BLE
  m.on_rotate(-1);
  CHECK(cfg.ble_enabled == 0);
  m.on_rotate(1);
  CHECK(cfg.ble_enabled == 1);
  m.on_click();

  m.on_rotate(2);  // CHANNEL — starts at omni
  m.on_click();
  m.on_rotate(1);
  CHECK(cfg.midi.midi_channel == 0);  // omni wraps to channel 1
  m.on_rotate(-1);
  CHECK(cfg.midi.midi_channel == neon::MidiRouteConfig::kTargetNone);
  m.on_click();

  m.on_rotate(1);  // GATE — starts off
  m.on_click();
  m.on_rotate(1);
  CHECK(cfg.midi.gate_target == 0);  // off wraps to CLK1
  m.on_rotate(4);
  CHECK(cfg.midi.gate_target == neon::MidiRouteConfig::kTargetRun);
  CHECK(m.take_dirty());
}

// ---------------------------------------------------------------------------
// every screen renders
// ---------------------------------------------------------------------------

TEST_CASE("every screen renders distinct, non-empty output") {
  neon::Config cfg;
  const neon::UiStatus st = playing_status();
  using S = neon::MenuModel::Screen;

  struct Nav {
    const char* name;
    int menu_index;
    bool descend;
  };
  const Nav kNavs[] = {
      {"outputs", 1, false}, {"network", 2, false},
      {"midi", 3, false},    {"system", 4, false},
      {"clk edit", 1, true},
  };

  std::string previous;
  for (const Nav& nav : kNavs) {
    neon::MenuModel m(&cfg);
    m.on_click();
    m.on_rotate(nav.menu_index);
    m.on_click();
    if (nav.descend) {
      m.on_click();
      CHECK(m.screen() == S::kOutputEdit);
    }

    neon::Framebuffer fb;
    neon::render_ui(m, st, fb);
    const std::string out = dump(fb);
    CAPTURE(nav.name);
    CHECK(lit_pixels(fb) > 40);
    CHECK(out != previous);
    previous = out;
  }

  // The confirm screen offers both choices and marks the selected one by
  // inversion, so YES and NO cannot render identically.
  neon::MenuModel confirm(&cfg);
  confirm.on_click();
  confirm.on_rotate(4);
  confirm.on_click();
  confirm.on_rotate(neon::MenuModel::kSystemRebootItem);
  confirm.on_click();
  CHECK(confirm.screen() == S::kConfirm);

  neon::Framebuffer no_fb;
  neon::render_ui(confirm, st, no_fb);
  confirm.on_rotate(1);
  neon::Framebuffer yes_fb;
  neon::render_ui(confirm, st, yes_fb);
  CHECK(lit_pixels(no_fb) > 40);
  CHECK(dump(no_fb) != dump(yes_fb));
}

TEST_CASE("focus is shown by inversion, not by colour we do not have") {
  neon::Config cfg;
  const neon::UiStatus st = playing_status();

  neon::MenuModel browsing(&cfg);
  browsing.on_click();
  browsing.on_rotate(1);
  browsing.on_click();  // Outputs
  browsing.on_click();  // CLK 1 edit

  neon::Framebuffer browse_fb;
  neon::render_ui(browsing, st, browse_fb);

  browsing.on_click();  // enter edit on the same row
  CHECK(browsing.editing());
  neon::Framebuffer edit_fb;
  neon::render_ui(browsing, st, edit_fb);

  // An inverted row lights far more pixels than a caret does.
  CHECK(lit_pixels(edit_fb) > lit_pixels(browse_fb));
  CHECK(dump(edit_fb) != dump(browse_fb));
}

// --- Parity parameters on the merged screens -------------------------

TEST_CASE("output roles and rhythm modes cycle through every option") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  m.on_click();    // Menu
  m.on_rotate(1);  // Outputs
  m.on_click();
  m.on_click();    // CLK 1 edit

  m.on_rotate(8);  // ROLE
  m.on_click();
  m.on_rotate(1);
  CHECK(cfg.engine.clocks[0].role == neon::OutputRole::kGate);
  m.on_rotate(3);
  CHECK(cfg.engine.clocks[0].role == neon::OutputRole::kResetStop);
  m.on_rotate(1);  // wraps back to the start
  CHECK(cfg.engine.clocks[0].role == neon::OutputRole::kClock);
  m.on_rotate(-1);  // and wraps the other way
  CHECK(cfg.engine.clocks[0].role == neon::OutputRole::kResetStop);
  m.on_click();

  m.on_rotate(1);  // FREE RUN
  m.on_click();
  m.on_rotate(1);
  CHECK(cfg.engine.clocks[0].free_run);
  m.on_click();

  m.on_rotate(1);  // RHYTHM
  m.on_click();
  m.on_rotate(3);
  CHECK(cfg.engine.clocks[0].rhythm ==
        neon::ClockOutputConfig::RhythmMode::kPattern);
  m.on_rotate(1);  // wraps back to a plain clock
  CHECK(cfg.engine.clocks[0].rhythm ==
        neon::ClockOutputConfig::RhythmMode::kAll);
  m.on_click();

  m.on_rotate(4);  // CHANCE
  m.on_click();
  m.on_rotate(-30);
  CHECK(cfg.engine.clocks[0].probability_pct == 70);
  CHECK(m.take_dirty());
}

TEST_CASE("system screen reaches reset edge, MIDI nudge, sync, brightness") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  m.on_click();    // Menu
  m.on_rotate(4);  // System
  m.on_click();

  m.on_rotate(6);  // RST EDGE
  m.on_click();
  m.on_rotate(1);
  CHECK(cfg.engine.reset_before_edge);
  m.on_click();

  m.on_rotate(1);  // MIDI NDG
  m.on_click();
  m.on_rotate(-4);
  CHECK(cfg.midi_nudge_us == -2000);
  m.on_click();

  m.on_rotate(1);  // SS SYNC
  m.on_click();
  m.on_rotate(-1);
  CHECK(cfg.start_stop_sync == 0);
  m.on_click();

  m.on_rotate(1);  // BRIGHT
  m.on_click();
  m.on_rotate(-8);
  CHECK(cfg.display_brightness == 191);
  m.on_rotate(-1000);
  CHECK(cfg.display_brightness == 0);  // clamped, not wrapped
  CHECK(m.take_dirty());
}

TEST_CASE("reset mode cycles through the at-stop option") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  m.on_click();    // Menu
  m.on_rotate(4);  // System
  m.on_click();
  m.on_rotate(1);  // RESET
  m.on_click();

  m.on_rotate(1);
  CHECK(cfg.engine.reset_mode == neon::ResetMode::kEveryBar);
  m.on_rotate(1);
  CHECK(cfg.engine.reset_mode == neon::ResetMode::kOff);
  m.on_rotate(1);
  CHECK(cfg.engine.reset_mode == neon::ResetMode::kAtStop);
  m.on_rotate(1);  // wraps
  CHECK(cfg.engine.reset_mode == neon::ResetMode::kStartOfPlay);
}

TEST_CASE("REBOOT stays the last system row after the parity additions") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  m.on_click();    // Menu
  m.on_rotate(4);  // System
  m.on_click();
  m.on_rotate(neon::MenuModel::kSystemRebootItem);
  CHECK(std::string(m.item_label(m.cursor())) == "REBOOT");
  m.on_click();
  CHECK(m.screen() == neon::MenuModel::Screen::kConfirm);
}

TEST_CASE("system menu can disable the big beat display") {
  neon::Config cfg;
  CHECK(cfg.big_beat_display == 1);
  neon::MenuModel m(&cfg);
  m.on_click();    // Menu
  m.on_rotate(4);  // System
  m.on_click();
  m.on_rotate(10);  // BEAT
  CHECK(std::string(m.item_label(m.cursor())) == "BEAT");
  m.on_click();
  m.on_rotate(-1);
  CHECK(cfg.big_beat_display == 0);
}

TEST_CASE("giant beat fills the panel and flips ink on 2 and 4") {
  auto render_beat = [](uint32_t phase) {
    neon::UiStatus st;
    st.playing = true;
    st.big_beat_display = true;
    st.quantum_beats = 4;
    st.phase_milli_beats = phase;
    neon::Config cfg;
    neon::MenuModel menu(&cfg);
    neon::Framebuffer fb;
    neon::render_ui(menu, st, fb);
    return fb;
  };

  neon::Framebuffer one = render_beat(0);      // beat 1
  neon::Framebuffer two = render_beat(1000);   // beat 2
  neon::Framebuffer three = render_beat(2000); // beat 3
  neon::Framebuffer four = render_beat(3000);  // beat 4

  CHECK(dump(one) != dump(two));
  CHECK(dump(two) != dump(three));
  CHECK(dump(three) != dump(four));
  CHECK(dump(one) != dump(three));

  // 2 px black safe zone on every beat, including the inverted ones.
  CHECK_FALSE(one.pixel(0, 0));
  CHECK_FALSE(one.pixel(127, 127));
  CHECK_FALSE(two.pixel(0, 0));
  CHECK_FALSE(two.pixel(127, 0));
  CHECK_FALSE(two.pixel(0, 127));
  CHECK_FALSE(two.pixel(127, 127));
  CHECK_FALSE(two.pixel(1, 64));
  CHECK_FALSE(two.pixel(126, 64));

  const int ink1 = lit_pixels(one);
  const int ink2 = lit_pixels(two);
  // 1 is white-on-black; 2 is black-on-white, so far more pixels are lit.
  CHECK(ink1 > 400);
  CHECK(ink2 > ink1);

  // Corners of the inner field: beat 2's white background reaches just
  // inside the border.
  CHECK(two.pixel(2, 2));
  CHECK_FALSE(one.pixel(2, 2));
}

TEST_CASE("giant beat is only the live screen while playing") {
  neon::UiStatus st;
  st.playing = true;
  st.big_beat_display = true;
  st.phase_milli_beats = 0;
  neon::Config cfg;
  neon::MenuModel menu(&cfg);
  menu.on_click();  // leave Home
  neon::Framebuffer fb;
  neon::render_ui(menu, st, fb);
  // Menu header rule is a full-width line; a giant 1 is not.
  CHECK(lit_in_row(fb, neon::ui::kHeaderRuleY) == neon::Framebuffer::kWidth);
}
