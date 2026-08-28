#pragma once

#include <cstdint>

// TRS MIDI out on Serial1 (31.25 kbaud, Type A wiring): the session-
// derived 24 PPQN clock plus Start/Stop, mirroring main/midi_service.cpp.
// Clock bytes are emitted from a 500 µs IntervalTimer so a long UI frame
// push cannot smear the clock stream; MIDI's own tolerance dwarfs the
// half-millisecond grid.
//
// No MIDI *in* on this target yet: Serial1 RX (pin 0) is reserved but
// not wired (docs/TEENSY41.md §5.3), so there is no parser, router, or
// MIDI clock sync-in here. When the RX path lands, the sync side is the
// Daisy shape (daisy/src/{midi_daisy.cpp,link_service_daisy.cpp}):
// SerialMidiParser -> MidiRouter sync tap -> SyncFollower in the link
// service — see docs/MIDI_PLL_PHASES_HANDOFF.md Phase C.
namespace miditrs {

void init();
void poll(int64_t now_us);  // transport bytes + config following

}  // namespace miditrs
