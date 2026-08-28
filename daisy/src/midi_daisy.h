#pragma once

#include <cstdint>

#include "neon/midi/sync_follower.hpp"

// TRS MIDI on the board's MIDI UART(s) @ 31250 baud.
//
// Out: session-derived 24 PPQN clock from a 500 µs TIM4 ISR (immune to
// UI and QSPI stalls), Start/Stop on transport changes. Same solve and
// staging pattern as the ESP and Teensy midi services. The UART
// peripheral and pins come from the board header (USART1 on the Seed;
// UART4 on the Pod and patch.init()).
//
// In (docs/DAISY.md §7.4): the wire bytes feed the portable
// SerialMidiParser -> MidiRouter — the same router the ESP32 runs for
// BLE MIDI — so notes gate a pulse output, pitch maps to the CV jack,
// CCs edit latency/shuffle, Start/Stop drives the transport, Program
// Change recalls presets, and the clock stream can be forwarded to the
// TRS output per the configured clock policy. The router's clock-sync
// tap (0xF8/FA/FB/FC with arrival timestamps, plus Song Position) feeds
// the link service's MidiClockPll follower directly — everything here
// runs on the single-threaded main loop, so no cross-task queue sits
// between the UART drain and the arbitration like on the ESP.
//
// Clock out while following MIDI clock in: the existing clock policy
// already expresses the thru-vs-regeneration choice. kIgnore emits the
// snapshot-derived clock, which while following *is* the PLL grid —
// clean regenerated clock. kReplace mutes the regenerated stream and
// forwards the sender's own bytes — thru. kMerge sends both.
namespace miditrs {

void init();
void poll(int64_t now_us);

// The link service's MIDI clock follower; the router sync tap calls
// on_event on it directly. Set once at service init, before poll runs.
void set_sync_follower(neon::midi::SyncFollower* follower);

}  // namespace miditrs
