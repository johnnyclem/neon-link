#pragma once

#include <cstddef>
#include <cstdint>

namespace halesp {

// TRS MIDI on UART1 @ 31250 (HARDWARE.md §5.3, Type A).
// tx_gpio / rx_gpio < 0 leaves that direction unconnected.
bool midi_uart_init(int tx_gpio, int rx_gpio = -1);
void midi_uart_send(const uint8_t* bytes, size_t len);
void midi_uart_send_byte(uint8_t b);
// IRAM: write the UART1 TX FIFO from the GPTimer ISR. No-op until init.
void midi_uart_send_isr(const uint8_t* bytes, size_t len);
// Non-blocking drain of the RX FIFO. 0 if RX was never pinned.
int midi_uart_read(uint8_t* buf, size_t cap);

// True once the UART is configured. Diagnostics for "is TX even up?".
bool midi_uart_ready();
// Total bytes handed to the TX FIFO since boot (both the buffered and the
// ISR path). A running counter, so a caller can log the delta to confirm
// the clock is actually reaching the wire.
uint32_t midi_uart_tx_count();

}  // namespace halesp
