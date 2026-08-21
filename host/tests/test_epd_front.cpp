#include <doctest.h>

#include <cstring>

#include "neon/config/model.hpp"
#include "neon/ui/epd_front.hpp"

TEST_CASE("boot splash waits for both buttons") {
  neon::Config cfg;
  neon::EpdFrontPanel ui(&cfg);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kSplash);
  CHECK_FALSE(ui.invert());
  ui.on_up(0);
  ui.on_confirm(0);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kSplash);
  CHECK(ui.take_nudge() == 0);
  ui.on_chord(0);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kLive);
}

TEST_CASE("live up/down nudge tempo and confirm toggles transport") {
  neon::Config cfg;
  neon::EpdFrontPanel ui(&cfg);
  ui.on_chord(0);
  ui.on_up(0);
  CHECK(ui.take_nudge() == 1);
  ui.on_down(0);
  ui.on_down(0);
  CHECK(ui.take_nudge() == -2);
  CHECK_FALSE(ui.take_toggle());
  ui.on_confirm(0);
  CHECK(ui.take_toggle());
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kLive);
}

TEST_CASE("chord opens inverted menu; second chord or idle returns live") {
  neon::Config cfg;
  neon::EpdFrontPanel ui(&cfg);
  ui.on_chord(500);
  ui.on_chord(1000);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kMenu);
  CHECK(ui.invert());
  ui.on_chord(2000);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kLive);
  CHECK_FALSE(ui.invert());

  ui.on_chord(3000);
  ui.tick(3000 + neon::EpdFrontPanel::kIdleUs);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kLive);
}

TEST_CASE("confirm edits TRS, cancel reverts, confirm keeps it") {
  neon::Config cfg;
  neon::EpdFrontPanel ui(&cfg);
  ui.on_chord(0);
  ui.on_chord(1);
  ui.on_down(1);  // TRS
  CHECK(std::strcmp(ui.item_label(ui.cursor()), "TRS") == 0);
  ui.on_confirm(0);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kEdit);
  ui.on_up(0);
  char v[16];
  ui.item_value(ui.cursor(), v, sizeof(v));
  CHECK(std::strcmp(v, "B") == 0);
  ui.on_cancel(0);
  ui.item_value(ui.cursor(), v, sizeof(v));
  CHECK(std::strcmp(v, "A") == 0);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kMenu);

  ui.on_confirm(0);
  ui.on_up(0);
  ui.on_confirm(0);
  CHECK(ui.take_dirty());
  CHECK(cfg.midi_trs_type == 1);
}

TEST_CASE("PPQN is read-only; quantum cycles 1/2/4/8") {
  neon::Config cfg;
  cfg.quantum_beats = 4;
  neon::EpdFrontPanel ui(&cfg);
  ui.on_chord(0);
  ui.on_chord(1);
  ui.on_confirm(1);  // PPQN
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kMenu);
  ui.on_down(0);
  ui.on_down(0);
  ui.on_down(0);  // QUANTUM
  CHECK(std::strcmp(ui.item_label(ui.cursor()), "QUANTUM") == 0);
  ui.on_confirm(0);
  ui.on_up(0);
  CHECK(cfg.quantum_beats == 8);
  ui.on_up(0);
  CHECK(cfg.quantum_beats == 1);
}

TEST_CASE("long cancel opens power popup; cancel choice returns live") {
  neon::Config cfg;
  neon::EpdFrontPanel ui(&cfg);
  ui.on_chord(0);
  ui.on_cancel_long(0);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kPower);
  CHECK(ui.power_cursor() == 2);
  ui.on_confirm(0);
  CHECK(ui.take_action() == neon::EpdFrontPanel::Action::kNone);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kLive);

  ui.on_cancel_long(10);
  ui.on_up(10);
  ui.on_up(10);  // Restart
  CHECK(ui.power_cursor() == 0);
  ui.on_confirm(10);
  CHECK(ui.take_action() == neon::EpdFrontPanel::Action::kReboot);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kSplash);
}

TEST_CASE("live cancel short does nothing") {
  neon::Config cfg;
  neon::EpdFrontPanel ui(&cfg);
  ui.on_chord(0);
  ui.on_cancel(0);
  CHECK(ui.mode() == neon::EpdFrontPanel::Mode::kLive);
  CHECK(ui.take_nudge() == 0);
  CHECK_FALSE(ui.take_toggle());
}
