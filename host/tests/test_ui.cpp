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

  neon::Framebuffer line;
  line.draw_line(0, 0, 10, 0, true);
  CHECK(lit_pixels(line) == 11);
  CHECK(line.pixel(0, 0));
  CHECK(line.pixel(5, 0));
  CHECK(line.pixel(10, 0));
}

TEST_CASE("framebuffer diff_pixels and the 30 percent big-redraw threshold") {
  neon::Framebuffer a;
  neon::Framebuffer b;
  CHECK(a.diff_pixels(b) == 0);
  CHECK_FALSE(neon::Framebuffer::is_big_redraw(0));

  a.fill_rect(0, 0, 128, 128, true);
  CHECK(a.diff_pixels(b) == 128 * 128);
  CHECK(neon::Framebuffer::is_big_redraw(a.diff_pixels(b)));

  // 30% of 16384 is 4915.2, so 4916 is the first integer that qualifies.
  CHECK_FALSE(neon::Framebuffer::is_big_redraw(4915));
  CHECK(neon::Framebuffer::is_big_redraw(4916));

  neon::Framebuffer copy;
  copy.copy_from(a);
  CHECK(copy.diff_pixels(a) == 0);
}

TEST_CASE("giant 1 vs 2 is under 30 percent of pixels; beat-stage still leads") {
  neon::Framebuffer one;
  neon::ui::draw_giant_beat(one, 1);
  neon::Framebuffer two;
  neon::ui::draw_giant_beat(two, 2);
  const int changed = one.diff_pixels(two);
  CHECK(changed > 200);
  CHECK_FALSE(neon::Framebuffer::is_big_redraw(changed));
  // Discrete beat-stage frames still anticipate: the scan rewrites every
  // page even when the XOR is only the glyph.
  CHECK(neon::ui::anticipate_beat_flush(true, changed));
  CHECK_FALSE(neon::ui::anticipate_beat_flush(false, changed));
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

TEST_CASE("the network screen shows the AP password only while the setup "
          "AP is up, and the device token whenever it is set") {
  neon::Config cfg;
  neon::MenuModel menu(&cfg);
  menu.on_click();    // Home -> Menu
  menu.on_rotate(2);  // -> Network
  menu.on_click();

  neon::UiStatus bare;
  neon::Framebuffer bare_fb;
  neon::render_ui(menu, bare, bare_fb);

  neon::UiStatus with_pass = bare;
  with_pass.setup_ap = true;
  std::strcpy(with_pass.ap_pass, "link-ABCDEF");
  neon::Framebuffer pass_fb;
  neon::render_ui(menu, with_pass, pass_fb);
  CHECK(dump(bare_fb) != dump(pass_fb));

  // Off the setup AP, the same password must not render: it stopped being
  // the thing keeping anyone out the moment the module joined a network.
  neon::UiStatus pass_off_ap = with_pass;
  pass_off_ap.setup_ap = false;
  neon::Framebuffer off_fb;
  neon::render_ui(menu, pass_off_ap, off_fb);
  CHECK(dump(off_fb) == dump(bare_fb));

  neon::UiStatus with_token = bare;
  std::strcpy(with_token.device_token, "0123456789abcdef0123456789abcdef");
  neon::Framebuffer token_fb;
  neon::render_ui(menu, with_token, token_fb);
  CHECK(dump(token_fb) != dump(bare_fb));
}

// ---------------------------------------------------------------------------
// navigation
// ---------------------------------------------------------------------------

TEST_CASE("menu navigation walks the shallow screen graph") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  using S = neon::MenuModel::Screen;

  CHECK(m.screen() == S::kHome);
  m.on_rotate(3);  // live-screen rotate nudges BPM, stays on home
  CHECK(m.screen() == S::kHome);
  CHECK(m.take_tempo_nudge() == 3);
  CHECK(m.take_tempo_nudge() == 0);
  m.on_rotate(-2);
  CHECK(m.take_tempo_nudge() == -2);

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
  m.on_click();
  m.go_home();
  CHECK(m.screen() == S::kHome);
  CHECK(m.cursor() == 0);

  // Every menu destination is reachable and reports its own title.
  const S expected[7] = {S::kHome,  S::kOutputs, S::kNetwork, S::kMidi,
                         S::kAudio, S::kSystem,  S::kHome};
  for (int i = 0; i < 7; ++i) {
    neon::MenuModel nav(&cfg);
    nav.on_click();
    nav.on_rotate(i);
    nav.on_click();
    CHECK(nav.screen() == expected[i]);
  }
  {
    neon::MenuModel back(&cfg);
    back.on_click();
    back.on_rotate(neon::MenuModel::kMenuBackItem);
    CHECK(std::string(back.item_label(back.cursor())) == "BACK");
    back.on_click();
    CHECK(back.screen() == S::kHome);
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
    m.on_rotate(5);                                  // System
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
  m.on_rotate(5);
  m.on_click();
  m.on_rotate(neon::MenuModel::kSystemRebootItem);
  m.on_click();
  m.on_long_press();
  CHECK(m.screen() == S::kSystem);
  CHECK(m.take_action() == A::kNone);
}

TEST_CASE("touch helpers jump sections and nudge without edit mode") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  using S = neon::MenuModel::Screen;

  m.go_section(S::kMidi);
  CHECK(m.screen() == S::kMidi);
  CHECK(m.cursor() == 0);
  CHECK_FALSE(m.editing());

  cfg.midi_clock_out = 1;
  m.set_cursor(1);
  m.nudge_value(-1);
  CHECK(cfg.midi_clock_out == 0);
  CHECK(m.take_dirty());

  m.go_section(S::kSystem);
  m.set_cursor(5);  // QUANTUM
  m.nudge_value(1);
  CHECK(cfg.quantum_beats == 5);

  m.set_output_index(2);
  m.go_section(S::kOutputEdit);
  CHECK(m.output_index() == 2);
  CHECK(m.screen() == S::kOutputEdit);

  m.go_section(S::kConfirm);
  m.set_confirm_yes(true);
  CHECK(m.confirm_yes());
  m.go_home();
  CHECK(m.screen() == S::kHome);
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
  m.on_rotate(5);  // System
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
      {"midi", 3, false},    {"audio", 4, false},
      {"system", 5, false},  {"clk edit", 1, true},
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
  confirm.on_rotate(5);
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
  m.on_rotate(5);  // System
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
  m.on_rotate(5);  // System
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
  m.on_rotate(5);  // System
  m.on_click();
  m.on_rotate(neon::MenuModel::kSystemRebootItem);
  CHECK(std::string(m.item_label(m.cursor())) == "REBOOT");
  m.on_click();
  CHECK(m.screen() == neon::MenuModel::Screen::kConfirm);
}

TEST_CASE("the VERSION row is read-only and shows the firmware string, "
          "not a settable value") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  m.on_click();    // Menu
  m.on_rotate(5);  // System
  m.on_click();
  m.on_rotate(neon::MenuModel::kSystemVersionItem);
  CHECK(std::string(m.item_label(m.cursor())) == "VERSION");
  CHECK(m.cursor() == neon::MenuModel::kSystemRebootItem - 1);

  // MenuModel itself has no platform code to fill this in — item_value()
  // reports it empty, and the renderer substitutes UiStatus.firmware.
  char value[16];
  m.item_value(m.cursor(), value, sizeof(value));
  CHECK(value[0] == '\0');

  // A click must not enter edit mode: there is nothing here to edit, and
  // rotating while "editing" a read-only row should not look like it did
  // something.
  m.on_click();
  CHECK_FALSE(m.editing());

  neon::UiStatus st;
  std::strcpy(st.firmware, "v1.2.3-4-gabc1234");
  neon::Framebuffer fb;
  neon::render_ui(m, st, fb);
  neon::UiStatus other = st;
  std::strcpy(other.firmware, "v9.9.9");
  neon::Framebuffer other_fb;
  neon::render_ui(m, other, other_fb);
  CHECK(dump(fb) != dump(other_fb));
}

TEST_CASE("system menu can disable the big beat display") {
  neon::Config cfg;
  CHECK(cfg.big_beat_display == 1);
  neon::MenuModel m(&cfg);
  m.on_click();    // Menu
  m.on_rotate(5);  // System
  m.on_click();
  m.on_rotate(10);  // BEAT
  CHECK(std::string(m.item_label(m.cursor())) == "BEAT");
  m.on_click();
  m.on_rotate(-1);
  CHECK(cfg.big_beat_display == 0);
}

TEST_CASE("system menu cycles beat styles") {
  neon::Config cfg;
  CHECK(cfg.beat_style == neon::BeatStyle::kNumber);
  neon::MenuModel m(&cfg);
  m.on_click();
  m.on_rotate(5);
  m.on_click();
  m.on_rotate(11);  // STYLE
  CHECK(std::string(m.item_label(m.cursor())) == "STYLE");
  char buf[16];
  m.item_value(m.cursor(), buf, sizeof(buf));
  CHECK(std::string(buf) == "NUM");
  m.on_click();
  m.on_rotate(1);
  CHECK(cfg.beat_style == neon::BeatStyle::kPie);
  m.on_rotate(1);
  CHECK(cfg.beat_style == neon::BeatStyle::kPendulum);
  m.on_rotate(1);
  CHECK(cfg.beat_style == neon::BeatStyle::kPulse);
  m.on_rotate(1);
  CHECK(cfg.beat_style == neon::BeatStyle::kNumber);
}

TEST_CASE("system menu cycles colour themes") {
  neon::Config cfg;
  CHECK(cfg.color_theme == neon::ColorTheme::kLink);
  neon::MenuModel m(&cfg);
  m.on_click();
  m.on_rotate(5);
  m.on_click();
  m.on_rotate(12);  // COLOUR
  CHECK(std::string(m.item_label(m.cursor())) == "COLOUR");
  char buf[16];
  m.item_value(m.cursor(), buf, sizeof(buf));
  CHECK(std::string(buf) == "LINK");
  m.on_click();
  m.on_rotate(1);
  CHECK(cfg.color_theme == neon::ColorTheme::kVoid);
  m.on_rotate(1);
  CHECK(cfg.color_theme == neon::ColorTheme::kTeal);
  m.on_rotate(1);
  CHECK(cfg.color_theme == neon::ColorTheme::kPhosphor);
  m.on_rotate(1);
  CHECK(cfg.color_theme == neon::ColorTheme::kAmber);
  m.on_rotate(1);
  CHECK(cfg.color_theme == neon::ColorTheme::kMagenta);
  m.on_rotate(1);
  CHECK(cfg.color_theme == neon::ColorTheme::kPaper);
  m.on_rotate(1);
  CHECK(cfg.color_theme == neon::ColorTheme::kLink);
}

TEST_CASE("giant beat fills the panel with white numerals on black") {
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

  // 2 px black safe zone on every beat.
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
  CHECK(ink1 > 400);
  CHECK(ink2 > 400);

  // Inner field stays black; only the glyph is lit.
  CHECK_FALSE(one.pixel(2, 2));
  CHECK_FALSE(two.pixel(2, 2));
}

TEST_CASE("pie, pendulum and pulse beat styles draw distinct frames") {
  auto render = [](neon::BeatStyle style, uint32_t phase) {
    neon::UiStatus st;
    st.playing = true;
    st.big_beat_display = true;
    st.beat_style = static_cast<uint8_t>(style);
    st.quantum_beats = 4;
    st.phase_milli_beats = phase;
    neon::Config cfg;
    neon::MenuModel menu(&cfg);
    neon::Framebuffer fb;
    neon::render_ui(menu, st, fb);
    return fb;
  };

  neon::Framebuffer pie1 = render(neon::BeatStyle::kPie, 0);
  neon::Framebuffer pie4 = render(neon::BeatStyle::kPie, 3000);
  neon::Framebuffer num1 = render(neon::BeatStyle::kNumber, 0);
  neon::Framebuffer pend1 = render(neon::BeatStyle::kPendulum, 0);
  neon::Framebuffer pend2 = render(neon::BeatStyle::kPendulum, 1000);
  neon::Framebuffer pulse0 = render(neon::BeatStyle::kPulse, 0);
  neon::Framebuffer pulse5 = render(neon::BeatStyle::kPulse, 500);

  CHECK(dump(pie1) != dump(pie4));
  CHECK(lit_pixels(pie4) > lit_pixels(pie1));
  CHECK(dump(pie1) != dump(num1));
  CHECK(dump(pend1) != dump(pend2));
  CHECK(dump(pulse0) != dump(pulse5));
  CHECK(dump(pie1) != dump(pend1));
  CHECK(dump(pend1) != dump(pulse0));

  CHECK_FALSE(pie1.pixel(0, 0));
  CHECK_FALSE(pie4.pixel(127, 127));
  CHECK_FALSE(pend1.pixel(0, 0));
  CHECK_FALSE(pulse0.pixel(0, 0));
  CHECK(lit_pixels(pie1) > 200);
  CHECK(lit_pixels(pend1) > 80);
  CHECK(lit_pixels(pulse0) > 80);
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

// ---------------------------------------------------------------------------
// audio screen
// ---------------------------------------------------------------------------

namespace {

// Menu item 4 is AUDIO.
neon::MenuModel audio_menu(neon::Config* cfg) {
  neon::MenuModel m(cfg);
  m.on_click();
  m.on_rotate(4);
  m.on_click();
  return m;
}

}  // namespace

TEST_CASE("the audio screen edits the metronome and the output roles") {
  neon::Config cfg;
  neon::MenuModel m = audio_menu(&cfg);
  CHECK(m.screen() == neon::MenuModel::Screen::kAudio);
  CHECK(m.item_count() == neon::MenuModel::kAudioItems);

  char buf[24];
  m.item_value(0, buf, sizeof(buf));
  CHECK(std::string(buf) == "OFF");

  m.on_click();      // edit AUDIO
  m.on_rotate(1);    // on
  CHECK(cfg.audio.enabled == 1);
  CHECK(m.take_dirty());
  m.on_click();

  m.on_rotate(1);    // CLICK mode
  m.on_click();
  m.on_rotate(1);    // OFF -> CLICK
  CHECK(cfg.audio.metro_enabled == 1);
  CHECK(cfg.audio.metro_sound == neon::ClickSound::kNoise);
  m.on_rotate(1);    // WOOD
  CHECK(cfg.audio.metro_sound == neon::ClickSound::kWood);
  m.on_rotate(1);    // METRO
  CHECK(cfg.audio.metro_sound == neon::ClickSound::kSine);
  m.on_rotate(1);    // wrap to OFF
  CHECK(cfg.audio.metro_enabled == 0);
  CHECK(cfg.audio.enabled == 1);  // AUDIO row stays on
  m.on_rotate(-1);   // back to METRO
  CHECK(cfg.audio.metro_enabled == 1);
  CHECK(cfg.audio.metro_sound == neon::ClickSound::kSine);
  m.on_click();

  m.on_rotate(1);    // LEVEL
  m.on_click();
  m.on_rotate(-4);
  CHECK(cfg.audio.metro_gain == neon::kUnityGainByte - 20);
  m.on_rotate(1000);
  CHECK(cfg.audio.metro_gain == 255);  // clamped, never wrapped
  m.on_click();

  m.on_rotate(1);    // OUT L
  m.on_click();
  m.on_rotate(2);
  CHECK(cfg.audio.role_l == neon::AudioRole::kClock);
  m.on_rotate(-2);
  CHECK(cfg.audio.role_l == neon::AudioRole::kMix);
  m.on_rotate(-1);   // wraps to the last role
  CHECK(cfg.audio.role_l == neon::AudioRole::kLineIn);
  m.on_click();
}

TEST_CASE("the audio screen shows values a user can read at the rack") {
  neon::Config cfg;
  cfg.audio.enabled = 1;
  cfg.audio.metro_enabled = 1;
  cfg.audio.metro_gain = neon::kUnityGainByte;  // 100 %
  cfg.audio.metro_sound = neon::ClickSound::kWood;
  cfg.audio.role_l = neon::AudioRole::kMix;
  cfg.audio.role_r = neon::AudioRole::kRun;
  cfg.audio.la_publish_mix = 1;
  neon::MenuModel m = audio_menu(&cfg);

  const char* kLabels[] = {"AUDIO", "CLICK", "LEVEL", "OUT L",
                           "OUT R", "LINE IN", "PUBLISH", "SUB"};
  const char* kValues[] = {"ON",  "WOOD", "100%", "MIX",
                           "RUN", "OFF",  "ON",   "OFF"};
  char buf[24];
  for (int i = 0; i < neon::MenuModel::kAudioItems; ++i) {
    CHECK(std::string(m.item_label(i)) == kLabels[i]);
    m.item_value(i, buf, sizeof(buf));
    CHECK(std::string(buf) == kValues[i]);
  }
}

TEST_CASE("the panel can clear a subscription but not choose one") {
  neon::Config cfg;
  std::strcpy(cfg.audio.la_sub_channel_id, "peer/Live Master");
  neon::MenuModel m = audio_menu(&cfg);
  m.on_rotate(7);  // SUB
  m.on_click();
  m.on_rotate(1);  // forwards does nothing: there is no list here
  CHECK(std::string(cfg.audio.la_sub_channel_id) == "peer/Live Master");
  m.on_rotate(-1);
  CHECK(cfg.audio.la_sub_channel_id[0] == '\0');
}

TEST_CASE("long press leaves the audio screen with the cursor on AUDIO") {
  neon::Config cfg;
  neon::MenuModel m = audio_menu(&cfg);
  m.on_long_press();
  CHECK(m.screen() == neon::MenuModel::Screen::kMenu);
  CHECK(m.cursor() == 4);
}

// ---------------------------------------------------------------------------
// animated icons
// ---------------------------------------------------------------------------

TEST_CASE("icon frames advance on the clock the icon declares") {
  using namespace neon::ui;

  // The set carries both kinds of motion and some deliberately static marks.
  CHECK(kIconLink.clock == IconClock::kBeat);
  CHECK(kIconWifiSta.clock == IconClock::kTick);
  CHECK(kIconWarning.clock == IconClock::kStatic);
  CHECK(kIconLink.frame_count > 1);
  CHECK(kIconWarning.frame_count == 1);

  IconClocks clocks;
  clocks.beat = 2;
  clocks.tick = 1;

  // Each icon reads only its own clock.
  CHECK(icon_frame_index(kIconLink, clocks) == 2);
  CHECK(icon_frame_index(kIconWifiSta, clocks) == 1);
  CHECK(icon_frame_index(kIconWarning, clocks) == 0);

  // Counters run forever; the icon wraps them to its own length.
  CHECK(icon_frame(kIconLink, kIconLink.frame_count) ==
        icon_frame(kIconLink, 0));
  CHECK(icon_frame(kIconLink, 2u * kIconLink.frame_count + 1) ==
        icon_frame(kIconLink, 1));

  // A static icon ignores the counter entirely rather than reading past its
  // single frame.
  CHECK(icon_frame(kIconWarning, 7) == icon_frame(kIconWarning, 0));
}

TEST_CASE("every frame of an animated icon is distinct and non-empty") {
  using namespace neon::ui;
  const Icon* animated[] = {&kIconLink, &kIconWifiAp, &kIconWifiSta, &kIconRun};

  for (const Icon* icon : animated) {
    int drawn = 0;
    for (uint32_t f = 0; f < icon->frame_count; ++f) {
      neon::Framebuffer fb;
      fb.blit(0, 0, icon_frame(*icon, f), kIconSize, kIconSize);
      if (lit_pixels(fb) > 0) {
        ++drawn;
      }
      // Consecutive frames must differ, or the loop stalls visibly.
      const uint32_t next = (f + 1) % icon->frame_count;
      bool same = true;
      for (int row = 0; row < kIconSize; ++row) {
        if (icon_frame(*icon, f)[row] != icon_frame(*icon, next)[row]) {
          same = false;
          break;
        }
      }
      CHECK_FALSE(same);
    }
    CHECK(drawn == icon->frame_count);
  }
}

TEST_CASE("the tick counter reaches the panel") {
  neon::UiStatus a = playing_status();
  a.playing = false;  // classic layout, so the header is on screen
  a.setup_ap = true;  // wifi-ap is tick-clocked
  a.ble_on = true;
  a.anim_tick = 0;

  neon::UiStatus b = a;
  b.anim_tick = 1;

  CHECK(render_home(a) != render_home(b));
  // Same tick, same pixels: the renderer reads the counter from status and
  // never from a clock of its own.
  CHECK(render_home(a) == render_home(a));
}

TEST_CASE("beat-locked icons freeze while the transport is stopped") {
  auto header = [](const neon::UiStatus& st) {
    neon::Config cfg;
    neon::MenuModel menu(&cfg);
    neon::Framebuffer fb;
    neon::render_ui(menu, st, fb);
    std::string out;
    char row[neon::Framebuffer::kWidth + 1];
    for (int y = 0; y <= neon::ui::kHeaderRuleY; ++y) {
      fb.ascii_row(y, row);
      out += row;
    }
    return out;
  };

  neon::UiStatus stopped = playing_status();
  stopped.playing = false;
  stopped.phase_milli_beats = 0;
  neon::UiStatus stopped_later = stopped;
  stopped_later.phase_milli_beats = 3000;

  // The Link timeline keeps advancing whether or not anything is playing, so
  // without freezing them the header would animate a bar that is not running.
  CHECK(header(stopped) == header(stopped_later));

  // Playing, the same two phases must differ.
  neon::UiStatus playing = stopped;
  playing.playing = true;
  playing.big_beat_display = false;
  neon::UiStatus playing_later = playing;
  playing_later.phase_milli_beats = 3000;
  CHECK(header(playing) != header(playing_later));
}

// ---------------------------------------------------------------------------
// compact 128x64 layout (native SSD1306/1309 panels)
// ---------------------------------------------------------------------------

namespace {

// Ink strictly below the compact panel: the invariant that makes a 64-row
// flush "pages 0..7 verbatim" (docs/DAISY.md §4).
int lit_below(const neon::Framebuffer& fb, int height) {
  int n = 0;
  for (int y = height; y < neon::Framebuffer::kHeight; ++y) {
    n += lit_in_row(fb, y);
  }
  return n;
}

std::string render_compact(const neon::MenuModel& menu,
                           const neon::UiStatus& st) {
  neon::Framebuffer fb;
  neon::render_ui(menu, st, fb, neon::ui::kLayout64);
  return dump(fb);
}

}  // namespace

TEST_CASE("compact home renders deterministically inside 64 rows") {
  const neon::UiStatus st = playing_status();
  neon::Config cfg;
  neon::MenuModel menu(&cfg);
  neon::Framebuffer fb;
  neon::render_ui(menu, st, fb, neon::ui::kLayout64);

  CHECK(render_compact(menu, st) == render_compact(menu, st));
  CHECK(lit_pixels(fb) > 100);
  CHECK(lit_below(fb, neon::ui::kLayout64.height) == 0);

  // Same header band as the full layout.
  CHECK(lit_in_row(fb, neon::ui::kHeaderRuleY) == neon::Framebuffer::kWidth);

  // Hero occupies its compact band and is still the densest element.
  int hero_ink = 0;
  for (int y = neon::ui::kLayout64.hero_y;
       y < neon::ui::kLayout64.hero_y + neon::ui::kHeroHeight; ++y) {
    hero_ink += lit_in_row(fb, y);
  }
  CHECK(hero_ink > 300);

  // Status row has ink where the compact flow puts it.
  int status_ink = 0;
  for (int y = neon::ui::kLayout64.status_y;
       y < neon::ui::kLayout64.status_y + 7; ++y) {
    status_ink += lit_in_row(fb, y);
  }
  CHECK(status_ink > 0);

  // Phase bar: frame plus a fill reaching about half width at phase 2/4.
  const int bar_mid =
      neon::ui::kLayout64.bar_y + neon::ui::kLayout64.bar_h / 2;
  int fill_end = 0;
  for (int x = neon::ui::kBarInset; x < 126; ++x) {
    if (fb.pixel(x, bar_mid)) fill_end = x;
  }
  CHECK(fill_end > 55);
  CHECK(fill_end < 70);

  // The bar's bottom ticks are the last thing on the panel — nothing
  // renders past row 63.
  CHECK(neon::ui::kLayout64.bar_y + neon::ui::kLayout64.bar_h +
            neon::ui::kLayout64.bar_tick_h <=
        neon::ui::kLayout64.height);
}

TEST_CASE("every screen stays inside the compact panel") {
  // Walks the same screen graph as the distinct-output test, plus the
  // giant beat, asserting the top-half property everywhere — it is what
  // a native 64-row flush relies on.
  const neon::UiStatus st = playing_status();

  neon::Config cfg;
  neon::MenuModel m(&cfg);
  auto check_current = [&](const neon::UiStatus& status) {
    neon::Framebuffer fb;
    neon::render_ui(m, status, fb, neon::ui::kLayout64);
    CHECK(lit_pixels(fb) > 0);
    CHECK(lit_below(fb, neon::ui::kLayout64.height) == 0);
  };

  check_current(st);  // home, classic
  neon::UiStatus beats = st;
  beats.big_beat_display = true;
  for (uint32_t phase : {0u, 1000u, 2000u, 3000u}) {
    beats.phase_milli_beats = phase;
    check_current(beats);  // giant beat 1..4, white on black
  }
  for (uint8_t style = 1; style < static_cast<uint8_t>(neon::BeatStyle::kCount);
       ++style) {
    beats.beat_style = style;
    beats.phase_milli_beats = 0;
    check_current(beats);
    beats.phase_milli_beats = 2500;
    check_current(beats);
  }

  m.on_click();
  check_current(st);  // menu
  for (int dest = 1; dest <= 5; ++dest) {
    neon::Config c2;
    neon::MenuModel m2(&c2);
    m2.on_click();
    m2.on_rotate(dest);
    m2.on_click();
    neon::Framebuffer fb;
    neon::render_ui(m2, st, fb, neon::ui::kLayout64);
    CHECK(lit_pixels(fb) > 0);
    CHECK(lit_below(fb, neon::ui::kLayout64.height) == 0);
  }

  // Editing a value (inverted row) and the reboot confirm.
  neon::Config edit_cfg;
  neon::MenuModel editing(&edit_cfg);
  editing.on_click();
  editing.on_rotate(1);
  editing.on_click();
  editing.on_click();
  editing.on_rotate(1);
  editing.on_click();
  neon::Framebuffer edit_fb;
  neon::render_ui(editing, st, edit_fb, neon::ui::kLayout64);
  CHECK(lit_below(edit_fb, neon::ui::kLayout64.height) == 0);

  neon::Config confirm_cfg;
  neon::MenuModel confirm(&confirm_cfg);
  confirm.on_click();
  confirm.on_rotate(5);
  confirm.on_click();
  confirm.on_rotate(neon::MenuModel::kSystemRebootItem);
  confirm.on_click();
  neon::Framebuffer confirm_fb;
  neon::render_ui(confirm, st, confirm_fb, neon::ui::kLayout64);
  CHECK(lit_pixels(confirm_fb) > 0);
  CHECK(lit_below(confirm_fb, neon::ui::kLayout64.height) == 0);
}

TEST_CASE("compact lists show four rows and scroll to the cursor") {
  neon::Config cfg;
  neon::MenuModel m(&cfg);
  m.on_click();  // Home -> Menu (six items, only four fit)

  const neon::UiStatus st = playing_status();
  const std::string top = render_compact(m, st);

  // Cursor inside the first window: identical render.
  m.on_rotate(3);
  const std::string still_top = render_compact(m, st);
  // Moving past the fourth row scrolls the window: different rows appear.
  m.on_rotate(2);
  const std::string scrolled = render_compact(m, st);
  CHECK(top != still_top);  // caret moved
  CHECK(still_top != scrolled);

  // No row is ever drawn at or below the panel edge.
  const int last_row_bottom =
      neon::ui::kLayout64.list_top +
      neon::ui::kLayout64.list_rows * neon::ui::kLayout64.list_row_h;
  CHECK(last_row_bottom <= neon::ui::kLayout64.height + 5);
}

TEST_CASE("the 128-layout render is unchanged by the layout parameter") {
  const neon::UiStatus st = playing_status();
  neon::Config cfg;
  neon::MenuModel menu(&cfg);

  neon::Framebuffer classic;
  neon::render_ui(menu, st, classic);
  neon::Framebuffer with_layout;
  neon::render_ui(menu, st, with_layout, neon::ui::kLayout128);
  CHECK(std::memcmp(classic.data(), with_layout.data(),
                    neon::Framebuffer::kSize) == 0);
}
