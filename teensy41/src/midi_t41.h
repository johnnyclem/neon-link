#pragma once

#include <cstdint>

// TRS MIDI out on Serial1 (31.25 kbaud, Type A wiring): the session-
// derived 24 PPQN clock plus Start/Stop, mirroring main/midi_service.cpp.
// Clock bytes are emitted from a 500 µs IntervalTimer so a long UI frame
// push cannot smear the clock stream; MIDI's own tolerance dwarfs the
// half-millisecond grid.
namespace miditrs {

void init();
void poll(int64_t now_us);  // transport bytes + config following

}  // namespace miditrs
