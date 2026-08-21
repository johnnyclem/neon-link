#include <doctest.h>

#include "neon/ui/epd_refresh.hpp"

using neon::EpdRefreshPlanner;
using Kind = neon::EpdRefreshPlanner::Kind;

namespace {
constexpr int64_t kSec = 1000000;
}

TEST_CASE("first paint after boot is a full refresh") {
  EpdRefreshPlanner p;
  CHECK(p.plan(0, /*changed=*/true, /*user=*/true, /*layout=*/false,
               /*asleep=*/false) == Kind::kFull);
  CHECK(p.plan(0, false, true, false, false) == Kind::kNone);
}

TEST_CASE("user changes after a base are immediate partials") {
  EpdRefreshPlanner p;
  p.note_painted(0, Kind::kFull, true);
  CHECK(p.plan(1000, true, true, false, false) == Kind::kPartial);
  p.note_painted(1000, Kind::kPartial, true);
  // No floor between user paints — the next nudge repaints right away.
  CHECK(p.plan(2000, true, true, false, false) == Kind::kPartial);
}

TEST_CASE("ambient changes keep their floors") {
  EpdRefreshPlanner p;
  p.note_painted(0, Kind::kFull, /*user=*/true);
  // Inside the 800 ms settle after a user paint: held.
  CHECK(p.plan(500000, true, false, false, false) == Kind::kNone);
  CHECK(p.plan(EpdRefreshPlanner::kUserSettleUs, true, false, false, false) ==
        Kind::kPartial);
  p.note_painted(EpdRefreshPlanner::kUserSettleUs, Kind::kPartial,
                 /*user=*/false);
  // Ambient-to-ambient floor is 5 s.
  const int64_t t0 = EpdRefreshPlanner::kUserSettleUs;
  CHECK(p.plan(t0 + 4 * kSec, true, false, false, false) == Kind::kNone);
  CHECK(p.plan(t0 + EpdRefreshPlanner::kAmbientFloorUs, true, false, false,
               false) == Kind::kPartial);
  // A user change ignores the ambient floor entirely.
  CHECK(p.plan(t0 + kSec, true, true, false, false) == Kind::kPartial);
}

TEST_CASE("layout flips force a full refresh") {
  EpdRefreshPlanner p;
  p.note_painted(0, Kind::kFull, true);
  CHECK(p.plan(1000, true, true, /*layout=*/true, false) == Kind::kFull);
  // A full repaint re-arms partials.
  p.note_painted(1000, Kind::kFull, true);
  CHECK(p.plan(2000, true, true, false, false) == Kind::kPartial);
}

TEST_CASE("ghosting hygiene: full after kMaxPartials partials") {
  EpdRefreshPlanner p;
  p.note_painted(0, Kind::kFull, true);
  int64_t t = 0;
  for (int i = 0; i < EpdRefreshPlanner::kMaxPartials; ++i) {
    t += 1000;
    REQUIRE(p.plan(t, true, true, false, false) == Kind::kPartial);
    p.note_painted(t, Kind::kPartial, true);
  }
  t += 1000;
  CHECK(p.plan(t, true, true, false, false) == Kind::kFull);
  p.note_painted(t, Kind::kFull, true);
  CHECK(p.plan(t + 1000, true, true, false, false) == Kind::kPartial);
}

TEST_CASE("sleep discards the base; a sleeping panel paints full") {
  EpdRefreshPlanner p;
  p.note_painted(0, Kind::kFull, true);
  CHECK_FALSE(p.sleep_due(0));
  CHECK(p.sleep_due(EpdRefreshPlanner::kSleepGraceUs));
  p.note_slept();
  // Whether asked with the asleep flag or after the wake-up, the next
  // paint must re-seed the base.
  CHECK(p.plan(EpdRefreshPlanner::kSleepGraceUs + 1, true, true, false,
               /*asleep=*/true) == Kind::kFull);
  CHECK(p.plan(EpdRefreshPlanner::kSleepGraceUs + 1, true, true, false,
               /*asleep=*/false) == Kind::kFull);
}

TEST_CASE("no spontaneous repaints while idle") {
  EpdRefreshPlanner p;
  p.note_painted(0, Kind::kFull, true);
  // Hours pass with no change: nothing to do, whatever the counters say.
  CHECK(p.plan(3600 * kSec, false, false, false, false) == Kind::kNone);
}
