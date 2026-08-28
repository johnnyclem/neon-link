#pragma once

// One poll's clock_source arbitration verdict (docs/SPIKE_MIDI_PLL.md
// §5.4), shared by every link service so the precedence contract lives —
// and is host-tested — in exactly one place: CLK IN outranks MIDI clock
// under kAuto (the jack is the module's native sync, and a beat-locked
// drum machine sending both would otherwise fight itself); each master
// mode pins its own source; the session is the fallback when neither
// wins. While not allowed, the MIDI follower keeps its PLL warm silently
// so a handover starts from a live estimate.

#include "neon/config/model.hpp"

namespace neon {

struct ClockArbitration {
  bool follow_clk_in = false;  // the jack owns tempo + phase this poll
  bool midi_allowed = false;   // the MIDI follower may publish this poll
};

constexpr ClockArbitration arbitrate_clock_source(ClockSource source,
                                                  bool clk_in_active) {
  ClockArbitration a;
  a.follow_clk_in = (source == ClockSource::kAuto ||
                     source == ClockSource::kExternalMaster) &&
                    clk_in_active;
  a.midi_allowed = (source == ClockSource::kAuto ||
                    source == ClockSource::kMidiMaster) &&
                   !a.follow_clk_in;
  return a;
}

}  // namespace neon
