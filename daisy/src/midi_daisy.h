#pragma once

#include <cstdint>

// TRS MIDI out on the board's MIDI UART @ 31250 baud: session-derived
// 24 PPQN clock
// from a 500 µs TIM4 ISR (immune to UI and QSPI stalls), Start/Stop on
// transport changes. Same solve and staging pattern as the ESP and
// Teensy midi services. The UART peripheral and pins come from the
// board header (USART1 on the Seed; UART4 on the Pod and patch.init()).
namespace miditrs {

void init();
void poll(int64_t now_us);

}  // namespace miditrs
