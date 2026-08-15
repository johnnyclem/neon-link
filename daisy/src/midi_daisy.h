#pragma once

#include <cstdint>

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
// TRS output per the configured clock policy. Independently of the
// router, every incoming 0xF8 tick is timestamped for the link
// service's MIDI clock-follow (pop_clock below): incoming MIDI clock is
// an external tempo source exactly like CLK IN, at a fixed 24 PPQN.
namespace miditrs {

void init();
void poll(int64_t now_us);

// Timestamps of received MIDI clock ticks (µs, shared timebase),
// drained by the link service each capture. Returns false when empty.
bool pop_clock(int64_t* t_us);

}  // namespace miditrs
