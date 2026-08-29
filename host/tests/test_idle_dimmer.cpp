#include <doctest.h>

#include "neon/ui/idle_dimmer.hpp"

using neon::ui::IdleDimmer;
using Level = neon::ui::IdleDimmer::Level;

namespace {
constexpr int64_t kSec = 1000000;
}

TEST_CASE("dim_after_s == 0 disables dimming entirely") {
  IdleDimmer d;
  d.configure(0, 64);
  d.note_activity(0);
  CHECK(d.level(3600 * kSec) == Level::kActive);
  CHECK(d.apply(200, 3600 * kSec) == 200);
  CHECK(d.frame_interval_hint_ms(3600 * kSec) == 0);
}

TEST_CASE("active -> dim -> blank on a stopped transport") {
  IdleDimmer d;
  d.configure(30, 64);
  d.note_activity(0);
  d.set_playing(false);
  CHECK(d.level(29 * kSec) == Level::kActive);
  CHECK(d.level(30 * kSec) == Level::kDim);
  CHECK(d.apply(200, 30 * kSec) == 64);
  // A user brightness below the dim level is never raised.
  CHECK(d.apply(20, 30 * kSec) == 20);
  CHECK(d.level(89 * kSec) == Level::kDim);
  CHECK(d.level(90 * kSec) == Level::kBlank);
  CHECK(d.apply(200, 90 * kSec) == 0);
  CHECK(d.frame_interval_hint_ms(90 * kSec) == IdleDimmer::kBlankFrameMs);
}

TEST_CASE("playing never blanks, only dims, and keeps the native rate") {
  IdleDimmer d;
  d.configure(30, 64);
  d.note_activity(0);
  d.set_playing(true);
  CHECK(d.level(30 * kSec) == Level::kDim);
  CHECK(d.level(3600 * kSec) == Level::kDim);
  // Beat animation must not stutter under a dimmed backlight.
  CHECK(d.frame_interval_hint_ms(3600 * kSec) == 0);
  // Stopping while long idle drops the rest of the way to blank.
  d.set_playing(false);
  CHECK(d.level(3600 * kSec) == Level::kBlank);
}

TEST_CASE("activity restores full brightness immediately") {
  IdleDimmer d;
  d.configure(30, 64);
  d.note_activity(0);
  CHECK(d.level(90 * kSec) == Level::kBlank);
  d.note_activity(90 * kSec);
  CHECK(d.level(90 * kSec + 1) == Level::kActive);
  CHECK(d.apply(200, 90 * kSec + 1) == 200);
}

TEST_CASE("stopped but dim keeps a relaxed frame hint") {
  IdleDimmer d;
  d.configure(30, 64);
  d.note_activity(0);
  d.set_playing(false);
  CHECK(d.frame_interval_hint_ms(30 * kSec) == IdleDimmer::kDimFrameMs);
}
