#pragma once

#include <cstdint>

// TRS MIDI out on USART1 @ 31250 baud: session-derived 24 PPQN clock
// from a 500 µs TIM4 ISR (immune to UI and QSPI stalls), Start/Stop on
// transport changes. Same solve and staging pattern as the ESP and
// Teensy midi services.
namespace miditrs {

void init();
void poll(int64_t now_us);

}  // namespace miditrs
