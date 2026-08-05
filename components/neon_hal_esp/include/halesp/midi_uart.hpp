#pragma once

#include <cstddef>
#include <cstdint>

namespace halesp {

// TRS MIDI out: UART TX-only at 31250 baud (HARDWARE.md §5.3, Type A).
bool midi_uart_init(int tx_gpio);
void midi_uart_send(const uint8_t* bytes, size_t len);
void midi_uart_send_byte(uint8_t b);

}  // namespace halesp
