#include "halesp/midi_uart.hpp"

#include "driver/uart.h"
#include "esp_attr.h"
#include "soc/uart_reg.h"

namespace halesp {

namespace {
constexpr uart_port_t kPort = UART_NUM_1;
bool g_ready = false;
}  // namespace

bool midi_uart_init(int tx_gpio) {
  uart_config_t cfg = {};
  cfg.baud_rate = 31250;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  if (uart_driver_install(kPort, 256, 512, 0, nullptr, 0) != ESP_OK) {
    return false;
  }
  if (uart_param_config(kPort, &cfg) != ESP_OK) {
    return false;
  }
  if (uart_set_pin(kPort, tx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                   UART_PIN_NO_CHANGE) != ESP_OK) {
    return false;
  }
  g_ready = true;
  return true;
}

void midi_uart_send(const uint8_t* bytes, size_t len) {
  if (g_ready) {
    uart_write_bytes(kPort, bytes, len);
  }
}

void midi_uart_send_byte(uint8_t b) { midi_uart_send(&b, 1); }

void IRAM_ATTR midi_uart_send_isr(const uint8_t* bytes, size_t len) {
  if (!g_ready || bytes == nullptr) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    WRITE_PERI_REG(UART_FIFO_AHB_REG(UART_NUM_1), bytes[i]);
  }
}

}  // namespace halesp
