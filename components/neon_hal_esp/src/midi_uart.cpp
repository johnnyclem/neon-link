#include "halesp/midi_uart.hpp"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "hal/uart_ll.h"

namespace halesp {

namespace {
constexpr uart_port_t kPort = UART_NUM_1;
// ~20 s of 24 PPQN at 120 BPM if the drain task is stalled. The old 256
// byte ring filled in ~5 s of clock-only MIDI IN; once full the IDF
// driver disables RX interrupts and the C3 UART stops delivering bytes
// until a reset.
constexpr int kRxBuf = 1024;
// Clock bytes are poked into the HW FIFO from the GPTimer ISR. A driver
// TX ring races that path (TXFIFO_EMPTY ISR vs. direct FIFO writes) and
// can stall the UART FSM — which on C3 does not recover without a reset.
constexpr int kTxBuf = 0;

bool g_ready = false;
volatile uint32_t g_tx_bytes = 0;
portMUX_TYPE g_tx_mux = portMUX_INITIALIZER_UNLOCKED;

// IRAM: write as many bytes as the HW TX FIFO will take. Never write a
// full FIFO — on ESP32-C3 that can freeze the UART until a chip reset.
size_t IRAM_ATTR write_fifo(const uint8_t* bytes, size_t len) {
  uart_dev_t* uart = UART_LL_GET_HW(kPort);
  size_t n = 0;
  while (n < len) {
    const uint32_t free = uart_ll_get_txfifo_len(uart);
    if (free == 0) {
      break;
    }
    uart_ll_write_txfifo(uart, bytes + n, 1);
    ++n;
  }
  g_tx_bytes += static_cast<uint32_t>(n);
  return n;
}

}  // namespace

bool midi_uart_init(int tx_gpio, int rx_gpio) {
  if (tx_gpio < 0 && rx_gpio < 0) {
    return false;
  }
  if (!g_ready) {
    uart_config_t cfg = {};
    cfg.baud_rate = 31250;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(kPort, kRxBuf, kTxBuf, 0, nullptr,
                            ESP_INTR_FLAG_IRAM) != ESP_OK) {
      return false;
    }
    if (uart_param_config(kPort, &cfg) != ESP_OK) {
      return false;
    }
    // Move bytes out of the 128-byte HW RX FIFO early so a unicore stall
    // does not overflow it (overflow disables RX until a flush).
    uart_set_rx_full_threshold(kPort, 8);
  }
  const int tx = tx_gpio >= 0 ? tx_gpio : UART_PIN_NO_CHANGE;
  const int rx = rx_gpio >= 0 ? rx_gpio : UART_PIN_NO_CHANGE;
  if (uart_set_pin(kPort, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) !=
      ESP_OK) {
    return false;
  }
  if (rx_gpio >= 0) {
    gpio_set_pull_mode(static_cast<gpio_num_t>(rx_gpio), GPIO_PULLUP_ONLY);
  }
  g_ready = true;
  return true;
}

void midi_uart_send(const uint8_t* bytes, size_t len) {
  if (!g_ready || bytes == nullptr || len == 0) {
    return;
  }
  // Task context: wait a few byte-times for FIFO room, then drop. Never
  // call uart_write_bytes — that path shares the FIFO with the ISR.
  size_t off = 0;
  for (int spin = 0; off < len && spin < 64; ++spin) {
    portENTER_CRITICAL(&g_tx_mux);
    off += write_fifo(bytes + off, len - off);
    portEXIT_CRITICAL(&g_tx_mux);
    if (off >= len) {
      return;
    }
    // ~320 µs per MIDI byte; a 1 ms tick is one extra byte-time of slack.
    vTaskDelay(1);
  }
}

void midi_uart_send_byte(uint8_t b) { midi_uart_send(&b, 1); }

void IRAM_ATTR midi_uart_send_isr(const uint8_t* bytes, size_t len) {
  if (!g_ready || bytes == nullptr || len == 0) {
    return;
  }
  portENTER_CRITICAL_ISR(&g_tx_mux);
  write_fifo(bytes, len);
  portEXIT_CRITICAL_ISR(&g_tx_mux);
}

bool midi_uart_ready() { return g_ready; }

uint32_t midi_uart_tx_count() { return g_tx_bytes; }

int midi_uart_read(uint8_t* buf, size_t cap) {
  if (!g_ready || buf == nullptr || cap == 0) {
    return 0;
  }
  const int n = uart_read_bytes(kPort, buf, cap, 0);
  return n > 0 ? n : 0;
}

}  // namespace halesp
