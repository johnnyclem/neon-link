#pragma once

// Converts captured Link session state (doubles, core-0 boundary) into the
// integer TimelineSnapshot the pulse engine consumes, deduplicating
// immaterial changes so the engine only re-anchors when the session
// actually moved.

#include "hal/ILinkSession.hpp"
#include "neon/timeline.hpp"

namespace neon {

// Builds `out` from `state`. Returns true if the result differs materially
// from `prev` (pass nullptr for the first capture — always true). Material:
//  - tempo change beyond ~0.005 BPM relative
//  - transport (playing) or peer-count change
//  - the new beat position deviating more than kBeatEpsilon from what
//    `prev` predicts for the same instant (session re-sync / phase jump)
bool build_snapshot(const hal::LinkState& state, const TimelineSnapshot* prev,
                    TimelineSnapshot& out);

// Deviation threshold in beats (~50 µs at 120 BPM).
inline constexpr double kBeatEpsilon = 1e-4;

}  // namespace neon
