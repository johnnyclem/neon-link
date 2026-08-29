#include <doctest.h>

#include "neon/ui/rlcd_refresh.hpp"

using neon::RlcdFramePlanner;
using Power = neon::RlcdFramePlanner::Power;

namespace {
constexpr int64_t kSec = 1000000;
}

TEST_CASE("nothing changed, nothing painted") {
  RlcdFramePlanner p;
  CHECK_FALSE(p.plan(0, /*changed=*/false, /*user=*/true));
  CHECK_FALSE(p.plan(kSec, false, false));
}

TEST_CASE("first change paints immediately") {
  RlcdFramePlanner p;
  CHECK(p.plan(0, true, false));
}

TEST_CASE("user paints are capped near 30 fps") {
  RlcdFramePlanner p;
  p.note_painted(0);
  CHECK_FALSE(p.plan(10000, true, true));
  CHECK(p.plan(RlcdFramePlanner::kUserFloorUs, true, true));
}

TEST_CASE("ambient churn is throttled but beats stay on time") {
  RlcdFramePlanner p;
  p.note_painted(0);
  CHECK_FALSE(p.plan(50000, true, false));
  // 100 ms floor: a beat tick at 120 BPM (500 ms) always lands.
  CHECK(p.plan(RlcdFramePlanner::kAmbientFloorUs, true, false));
}

TEST_CASE("panel drops to LPM after idle and wakes on activity") {
  RlcdFramePlanner p;
  CHECK(p.power(0) == Power::kHpm);  // boot
  p.note_painted(0);
  CHECK(p.power(kSec) == Power::kHpm);
  CHECK(p.power(RlcdFramePlanner::kLpmAfterUs) == Power::kLpm);
  p.note_user(RlcdFramePlanner::kLpmAfterUs);
  CHECK(p.power(RlcdFramePlanner::kLpmAfterUs + 1) == Power::kHpm);
}
