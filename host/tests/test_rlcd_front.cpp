#include <doctest.h>

#include <cstring>

#include "neon/ui/rlcd_front.hpp"

using neon::Config;
using neon::RlcdFrontPanel;
using Mode = neon::RlcdFrontPanel::Mode;
using Action = neon::RlcdFrontPanel::Action;

TEST_CASE("splash clears on any button or after the timeout") {
  Config cfg;
  {
    RlcdFrontPanel ui(&cfg);
    CHECK(ui.mode() == Mode::kSplash);
    ui.on_boot_short(1);
    CHECK(ui.mode() == Mode::kLive);
  }
  {
    RlcdFrontPanel ui(&cfg);
    ui.tick(1);
    CHECK(ui.mode() == Mode::kSplash);
    ui.tick(1 + RlcdFrontPanel::kSplashUs);
    CHECK(ui.mode() == Mode::kLive);
  }
}

TEST_CASE("live: KEY toggles transport, BOOT taps up, BOOT hold opens tempo") {
  Config cfg;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);  // leave splash
  ui.on_key_short(2);
  CHECK(ui.take_toggle());
  CHECK_FALSE(ui.take_toggle());
  ui.on_boot_short(3);  // +1
  ui.on_boot_short(4);  // +1
  CHECK(ui.take_nudge() == 2);
  ui.on_boot_long(5);  // opens the Tempo screen, no change on entry
  CHECK(ui.mode() == Mode::kTempo);
  CHECK(ui.take_nudge() == 0);
}

TEST_CASE("tempo screen: BOOT up, KEY down, hold auto-repeats, idle closes") {
  Config cfg;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);   // leave splash -> live
  ui.on_boot_long(2);   // open the Tempo screen
  CHECK(ui.mode() == Mode::kTempo);

  ui.on_boot_short(3);  // +1
  ui.on_boot_short(4);  // +1
  ui.on_key_short(5);   // -1
  CHECK(ui.take_nudge() == 1);

  // Holding BOOT ramps up: long then repeats keep firing.
  ui.on_boot_long(6);
  ui.on_boot_repeat(7);
  ui.on_boot_repeat(8);
  CHECK(ui.take_nudge() == 3);

  // Holding KEY ramps down the same way.
  ui.on_key_long(9);
  ui.on_key_repeat(10);
  CHECK(ui.take_nudge() == -2);

  // KEY hold in tempo = -1 (and keeps the screen alive).
  ui.on_key_long(11);
  CHECK(ui.take_nudge() == -1);

  // Idle for the short tempo window returns to live.
  ui.tick(11 + RlcdFrontPanel::kTempoIdleUs);
  CHECK(ui.mode() == Mode::kLive);
}

TEST_CASE("auto-repeat is inert outside the tempo screen") {
  Config cfg;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);   // live
  ui.on_boot_repeat(2);
  ui.on_key_repeat(3);
  CHECK(ui.take_nudge() == 0);
  CHECK(ui.mode() == Mode::kLive);
}

TEST_CASE("menu navigation with two buttons") {
  Config cfg;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);
  ui.on_key_long(2);
  CHECK(ui.mode() == Mode::kMenu);
  CHECK(ui.cursor() == 0);
  ui.on_boot_short(3);
  CHECK(ui.cursor() == 1);
  ui.on_boot_long(4);
  ui.on_boot_long(5);
  CHECK(ui.cursor() == RlcdFrontPanel::kItems - 1);  // wrapped up
  ui.on_key_long(6);
  CHECK(ui.mode() == Mode::kLive);
}

TEST_CASE("portrait reverses menu navigation so the top button walks up") {
  Config cfg;
  cfg.display_portrait = 1;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);
  ui.on_key_long(2);
  CHECK(ui.mode() == Mode::kMenu);
  CHECK(ui.cursor() == 0);
  ui.on_boot_short(3);  // BOOT (top button in portrait) taps up -> wraps
  CHECK(ui.cursor() == RlcdFrontPanel::kItems - 1);
  ui.on_boot_long(4);   // BOOT hold walks down
  CHECK(ui.cursor() == 0);
}

TEST_CASE("editing TRS commits on KEY short and reverts on KEY long") {
  Config cfg;
  cfg.midi_trs_type = 0;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);
  ui.on_key_long(2);
  ui.on_boot_short(3);  // cursor -> 1 = TRS
  ui.on_key_short(4);
  CHECK(ui.mode() == Mode::kEdit);
  ui.on_boot_short(5);
  CHECK(cfg.midi_trs_type == 1);
  ui.on_key_short(6);  // commit
  CHECK(ui.mode() == Mode::kMenu);
  CHECK(ui.take_dirty());
  CHECK(cfg.midi_trs_type == 1);

  ui.on_key_short(7);  // re-enter edit
  ui.on_boot_short(8);
  CHECK(cfg.midi_trs_type == 0);
  ui.on_key_long(9);  // cancel
  CHECK(ui.mode() == Mode::kMenu);
  CHECK_FALSE(ui.take_dirty());
  CHECK(cfg.midi_trs_type == 1);  // reverted
}

TEST_CASE("readonly PPQN row does not enter edit") {
  Config cfg;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);
  ui.on_key_long(2);
  CHECK(ui.cursor() == 0);
  ui.on_key_short(3);
  CHECK(ui.mode() == Mode::kMenu);
}

TEST_CASE("power popup lives behind the POWER menu row") {
  Config cfg;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);
  ui.on_key_long(2);
  ui.on_boot_long(3);  // wrap up to the POWER row
  CHECK(ui.cursor() == RlcdFrontPanel::kPowerItem);
  ui.on_key_short(4);
  CHECK(ui.mode() == Mode::kPower);
  CHECK(ui.power_cursor() == 2);  // starts on CANCEL
  ui.on_key_short(5);
  CHECK(ui.mode() == Mode::kLive);
  CHECK(ui.take_action() == Action::kNone);

  // Restart.
  ui.on_key_long(6);
  ui.on_boot_long(7);
  ui.on_key_short(8);
  ui.on_boot_short(9);  // CANCEL -> RESTART
  CHECK(ui.power_cursor() == 0);
  ui.on_key_short(10);
  CHECK(ui.take_action() == Action::kReboot);
}

TEST_CASE("quantum steps through the ladder and the value renders") {
  Config cfg;
  cfg.quantum_beats = 4;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);
  ui.on_key_long(2);
  for (int i = 0; i < 3; ++i) {
    ui.on_boot_short(3 + i);  // cursor -> 3 = QUANTUM
  }
  CHECK(ui.cursor() == 3);
  ui.on_key_short(10);
  ui.on_boot_short(11);
  CHECK(cfg.quantum_beats == 8);
  ui.on_boot_short(12);
  CHECK(cfg.quantum_beats == 1);  // wrapped
  ui.on_boot_long(13);
  CHECK(cfg.quantum_beats == 8);
  char v[16];
  ui.item_value(3, v, sizeof(v));
  CHECK(std::strcmp(v, "8") == 0);
}

TEST_CASE("SCREEN row toggles portrait and renders its value") {
  Config cfg;
  cfg.display_portrait = 0;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);
  ui.on_key_long(2);
  for (int i = 0; i < 6; ++i) {
    ui.on_boot_short(3 + i);  // cursor -> 6 = SCREEN
  }
  CHECK(ui.cursor() == 6);
  char v[16];
  ui.item_value(6, v, sizeof(v));
  CHECK(std::strcmp(v, "LAND") == 0);

  ui.on_key_short(10);   // enter edit
  ui.on_boot_short(11);  // toggle
  CHECK(cfg.display_portrait == 1);
  ui.item_value(6, v, sizeof(v));
  CHECK(std::strcmp(v, "PORT") == 0);
  ui.on_key_short(12);  // commit
  CHECK(ui.mode() == Mode::kMenu);
  CHECK(ui.take_dirty());
}

TEST_CASE("THEME row cycles the mono theme and reverts on cancel") {
  Config cfg;
  cfg.mono_theme = neon::MonoTheme::kClassic;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);
  ui.on_key_long(2);
  for (int i = 0; i < 7; ++i) {
    ui.on_boot_short(3 + i);  // cursor -> 7 = THEME
  }
  CHECK(ui.cursor() == 7);
  char v[16];
  ui.item_value(7, v, sizeof(v));
  CHECK(std::strcmp(v, "CLASSIC") == 0);

  ui.on_key_short(20);   // enter edit
  ui.on_boot_short(21);  // CLASSIC -> INK
  CHECK(cfg.mono_theme == neon::MonoTheme::kInk);
  ui.item_value(7, v, sizeof(v));
  CHECK(std::strcmp(v, "INK") == 0);
  ui.on_boot_long(22);  // back down -> CLASSIC
  ui.on_boot_long(23);  // wraps to NIGHT
  CHECK(cfg.mono_theme == neon::MonoTheme::kNight);
  ui.on_key_long(24);  // cancel
  CHECK(cfg.mono_theme == neon::MonoTheme::kClassic);
  CHECK_FALSE(ui.take_dirty());

  ui.on_key_short(25);   // re-enter edit
  ui.on_boot_short(26);  // -> INK
  ui.on_key_short(27);   // commit
  CHECK(ui.take_dirty());
  CHECK(cfg.mono_theme == neon::MonoTheme::kInk);
}

TEST_CASE("menu idles back to live and reverts a pending edit") {
  Config cfg;
  cfg.start_stop_sync = 1;
  RlcdFrontPanel ui(&cfg);
  ui.on_key_short(1);
  ui.on_key_long(2);
  for (int i = 0; i < 4; ++i) {
    ui.on_boot_short(3 + i);  // cursor -> 4 = SS SYNC
  }
  ui.on_key_short(10);
  ui.on_boot_short(11);
  CHECK(cfg.start_stop_sync == 0);
  ui.tick(11 + RlcdFrontPanel::kIdleUs);
  CHECK(ui.mode() == Mode::kLive);
  CHECK(cfg.start_stop_sync == 1);  // reverted, never applied
  CHECK_FALSE(ui.take_dirty());
}
