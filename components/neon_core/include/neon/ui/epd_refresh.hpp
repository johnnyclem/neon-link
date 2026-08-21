#pragma once

#include <cstdint>

namespace neon {

// Refresh policy for the 5.79" panel. Pure logic, host-tested; the
// e-paper task feeds it observations and obeys its verdicts. It never
// talks to the driver.
//
// The panel (dual SSD1683) has two refresh modes: a full refresh
// (0xF7, seconds, flashes the glass, clears ghosting) and a partial
// refresh (0xFF, sub-second, no flash) that diffs against the "old"
// RAM seeded by the last full refresh. Partials are what make key
// interactions (BPM nudge, menu cursor, value edits) feel live, but
// they accumulate ghosting and are only valid while the seeded base
// survives — deep sleep is exited via hardware reset, which discards
// it.
//
// Policy:
//   - No base (boot, or the panel slept) → full.
//   - The layout flipped (overlay mode or inversion changed, i.e. most
//     of the glass is about to change) → full, so the big transition
//     also wipes accumulated ghosting.
//   - Every kMaxPartials partials → full, as ghosting hygiene.
//   - Otherwise → partial.
//   - User-driven changes paint immediately; ambient changes (peers,
//     WiFi, remote tempo) keep the old floors so the glass is not
//     nervous: 5 s between ambient paints, 800 ms after a user paint.
//   - The glass must not sit at high voltage forever: sleep_due() says
//     when the idle panel should be put back to deep sleep.
class EpdRefreshPlanner {
 public:
  enum class Kind : uint8_t { kNone, kPartial, kFull };

  static constexpr int kMaxPartials = 20;
  static constexpr int64_t kAmbientFloorUs = 5000000;
  static constexpr int64_t kUserSettleUs = 800000;
  static constexpr int64_t kSleepGraceUs = 30000000;

  // `asleep` = the panel is in deep sleep right now (waking it resets
  // the controller, so any paint that follows must re-seed the base).
  Kind plan(int64_t now_us, bool changed, bool user, bool layout_changed,
            bool asleep) const {
    if (!changed) {
      return Kind::kNone;
    }
    if (!user && now_us < ambient_ok_us_) {
      return Kind::kNone;
    }
    if (asleep || !base_valid_ || layout_changed ||
        partials_since_full_ >= kMaxPartials) {
      return Kind::kFull;
    }
    return Kind::kPartial;
  }

  void note_painted(int64_t now_us, Kind kind, bool user) {
    if (kind == Kind::kFull) {
      base_valid_ = true;
      partials_since_full_ = 0;
    } else if (kind == Kind::kPartial) {
      ++partials_since_full_;
    }
    last_paint_us_ = now_us;
    ambient_ok_us_ = now_us + (user ? kUserSettleUs : kAmbientFloorUs);
  }

  // The task put the panel to deep sleep; the seeded base will not
  // survive the wake-up reset.
  void note_slept() { base_valid_ = false; }

  bool sleep_due(int64_t now_us) const {
    return last_paint_us_ >= 0 && now_us - last_paint_us_ >= kSleepGraceUs;
  }

 private:
  bool base_valid_ = false;
  int partials_since_full_ = 0;
  int64_t last_paint_us_ = -1;
  int64_t ambient_ok_us_ = 0;
};

}  // namespace neon
