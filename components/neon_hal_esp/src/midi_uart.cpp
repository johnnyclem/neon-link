#include "halesp/midi_uart.hpp"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "soc/uart_reg.h"

namespace halesp {

namespace {
constexpr uart_port_t kPort = UART_NUM_1;
bool g_ready = false;
volatile uint32_t g_tx_bytes = 0;
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
    if (uart_driver_install(kPort, 256, 512, 0, nullptr, 0) != ESP_OK) {
      return false;
    }
    if (uart_param_config(kPort, &cfg) != ESP_OK) {
      return false;
    }
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
  if (g_ready) {
    uart_write_bytes(kPort, bytes, len);
    g_tx_bytes += static_cast<uint32_t>(len);
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
  g_tx_bytes += static_cast<uint32_t>(len);
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
