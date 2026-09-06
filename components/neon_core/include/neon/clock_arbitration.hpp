#pragma once

// One poll's clock_source arbitration verdict (docs/SPIKE_MIDI_PLL.md
// §5.4), shared by every link service so the precedence contract lives —
// and is host-tested — in exactly one place: under kAuto, CLK IN outranks
// MIDI clock, which outranks audio-follow. Each master mode pins its own
// source; kLinkMaster ignores all three.

#include "neon/config/model.hpp"

namespace neon {

struct ClockArbitration {
  bool follow_clk_in = false;  // the jack owns tempo + phase this poll
  bool midi_allowed = false;   // the MIDI follower may publish this poll
  bool audio_allowed = false;  // kAuto permission when CLK IN is silent
};

constexpr ClockArbitration arbitrate_clock_source(
    ClockSource source, bool clk_in_active, bool audio_follow_enabled) {
  ClockArbitration a;
  a.follow_clk_in = (source == ClockSource::kAuto ||
                     source == ClockSource::kExternalMaster) &&
                    clk_in_active;
  a.midi_allowed = (source == ClockSource::kAuto ||
                    source == ClockSource::kMidiMaster) &&
                   !a.follow_clk_in;
  a.audio_allowed = audio_follow_enabled && source == ClockSource::kAuto &&
                    !a.follow_clk_in;
  return a;
}

}  // namespace neon
