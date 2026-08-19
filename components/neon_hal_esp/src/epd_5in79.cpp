// Dual-SSD1683 5.79" driver. Command sequence from Waveshare's
// Arduino/epd5in79 (MIT, Copyright (C) Waveshare 2024/03/06).
//
// Two physical pin maps exist for the same panel:
//   CrowPanel all-in-one:  CS45 DC46 RST47 BUSY48 PWR7  SCK12 MOSI11
//   DevKit + 9-pin module: CS10 DC9  RST8  BUSY18 PWR7  SCK12 MOSI11
// Init probes RST/BUSY pairs and uses the map whose BUSY actually moves.

#include "halesp/epd_5in79.hpp"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace halesp {

namespace {

const char* kTag = "epd5in79";
constexpr int kWidth = 792;
constexpr int kHeight = 272;

int g_cs = -1;
int g_dc = -1;
int g_rst = -1;
int g_busy = -1;
spi_device_handle_t g_spi = nullptr;
bool g_ready = false;

struct PinMap {
  const char* name;
  int sck;
  int mosi;
  int cs;
  int dc;
  int rst;
  int busy;
  int pwr;
};

void delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void as_out(int pin, int level) {
  gpio_reset_pin(static_cast<gpio_num_t>(pin));
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
}

void as_in(int pin) {
  gpio_reset_pin(static_cast<gpio_num_t>(pin));
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT);
  gpio_pulldown_en(static_cast<gpio_num_t>(pin));
}

int busy_level() {
  return gpio_get_level(static_cast<gpio_num_t>(g_busy));
}

void cs_low() { gpio_set_level(static_cast<gpio_num_t>(g_cs), 0); }
void cs_high() { gpio_set_level(static_cast<gpio_num_t>(g_cs), 1); }

void spi_byte(const uint8_t* p, size_t n) {
  while (n > 0) {
    const size_t chunk = n > 64 ? 64 : n;
    spi_transaction_t t = {};
    t.length = chunk * 8;
    t.tx_buffer = p;
    spi_device_polling_transmit(g_spi, &t);
    p += chunk;
    n -= chunk;
  }
}

void cmd(uint8_t c) {
  gpio_set_level(static_cast<gpio_num_t>(g_dc), 0);
  cs_low();
  spi_byte(&c, 1);
  cs_high();
}

void data(uint8_t d) {
  gpio_set_level(static_cast<gpio_num_t>(g_dc), 1);
  cs_low();
  spi_byte(&d, 1);
  cs_high();
}

void data_buf(const uint8_t* p, size_t n) {
  gpio_set_level(static_cast<gpio_num_t>(g_dc), 1);
  cs_low();
  spi_byte(p, n);
  cs_high();
}

void wait_busy() {
  // Waveshare Arduino waits while BUSY == 1 (high = busy).
  const int64_t t0 = esp_timer_get_time();
  while (busy_level() == 1) {
    if (esp_timer_get_time() - t0 > 8000000) {
      ESP_LOGW(kTag, "busy timeout (pin %d still high after 8s)", g_busy);
      return;
    }
    delay_ms(20);
  }
  const int64_t dt = esp_timer_get_time() - t0;
  if (dt > 50000) {
    ESP_LOGI(kTag, "busy %d idle after %lld ms", g_busy, dt / 1000);
  }
}

void reset_hw() {
  gpio_set_level(static_cast<gpio_num_t>(g_rst), 0);
  delay_ms(20);
  gpio_set_level(static_cast<gpio_num_t>(g_rst), 1);
  delay_ms(20);
}

bool probe_busy(int rst, int busy, int pwr) {
  if (pwr >= 0) {
    as_out(pwr, 1);
    delay_ms(80);
  }
  as_in(busy);
  as_out(rst, 1);
  delay_ms(10);
  const int idle0 = gpio_get_level(static_cast<gpio_num_t>(busy));
  gpio_set_level(static_cast<gpio_num_t>(rst), 0);
  delay_ms(20);
  gpio_set_level(static_cast<gpio_num_t>(rst), 1);

  bool saw_high = idle0 == 1;
  bool saw_low_after = false;
  for (int i = 0; i < 30; ++i) {
    const int lv = gpio_get_level(static_cast<gpio_num_t>(busy));
    if (lv == 1) {
      saw_high = true;
    } else if (saw_high) {
      saw_low_after = true;
      break;
    }
    delay_ms(20);
  }
  ESP_LOGI(kTag, "probe RST=%d BUSY=%d idle0=%d high=%d fall=%d now=%d", rst,
           busy, idle0, saw_high ? 1 : 0, saw_low_after ? 1 : 0,
           gpio_get_level(static_cast<gpio_num_t>(busy)));
  return saw_high;
}

void window() {
  cmd(0x11);
  data(0x01);
  cmd(0x44);
  data(0x00);
  data(0x31);
  cmd(0x45);
  data(0x0f);
  data(0x01);
  data(0x00);
  data(0x00);
  cmd(0x4e);
  data(0x00);
  cmd(0x4f);
  data(0x0f);
  data(0x01);
  wait_busy();
  cmd(0x91);
  data(0x00);
  cmd(0xc4);
  data(0x31);
  data(0x00);
  cmd(0xc5);
  data(0x0f);
  data(0x01);
  data(0x00);
  data(0x00);
  cmd(0xce);
  data(0x31);
  cmd(0xcf);
  data(0x0f);
  data(0x01);
  wait_busy();
}

void turn_on(uint8_t mode) {
  cmd(0x22);
  data(mode);
  cmd(0x20);
  wait_busy();
}

bool start_spi(const PinMap& m) {
  g_cs = m.cs;
  g_dc = m.dc;
  g_rst = m.rst;
  g_busy = m.busy;

  as_out(m.cs, 1);
  as_out(m.dc, 1);
  as_out(m.rst, 1);
  if (m.pwr >= 0) {
    as_out(m.pwr, 1);
    delay_ms(120);
  }
  as_in(m.busy);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = m.mosi;
  bus.miso_io_num = -1;
  bus.sclk_io_num = m.sck;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 4096;
  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err == ESP_ERR_INVALID_STATE) {
    spi_bus_free(SPI2_HOST);
    err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  }
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "SPI bus init failed (%s)", esp_err_to_name(err));
    return false;
  }
  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = 2000000;
  dev.mode = 0;
  dev.spics_io_num = -1;  // manual CS — DC must be valid before CS falls
  dev.queue_size = 1;
  if (spi_bus_add_device(SPI2_HOST, &dev, &g_spi) != ESP_OK) {
    ESP_LOGE(kTag, "SPI device add failed");
    return false;
  }
  return true;
}

}  // namespace

bool epd5in79_init(int sck, int mosi, int cs, int dc, int rst, int busy,
                   int pwr) {
  if (sck < 0 || mosi < 0 || cs < 0 || dc < 0 || rst < 0 || busy < 0) {
    ESP_LOGE(kTag, "e-paper pins incomplete");
    return false;
  }

  const PinMap preferred{ "board_pins", sck, mosi, cs, dc, rst, busy, pwr };
  const PinMap maps[] = {
      preferred,
      { "crowpanel", 12, 11, 45, 46, 47, 48, 7 },
      { "devkit-module", 12, 11, 10, 9, 8, 18, 7 },
      { "ws-s3-epaper", 12, 13, 11, 10, 9, 8, 6 },
  };

  const PinMap* chosen = &maps[0];
  bool probed = false;
  for (const PinMap& m : maps) {
    if (probe_busy(m.rst, m.busy, m.pwr)) {
      chosen = &m;
      probed = true;
      ESP_LOGI(kTag, "using %s pin map (BUSY responded)", m.name);
      break;
    }
  }
  if (!probed) {
    ESP_LOGW(kTag, "no BUSY response; falling back to crowpanel");
    chosen = &maps[1];
  }

  if (!start_spi(*chosen)) {
    return false;
  }

  reset_hw();
  wait_busy();
  cmd(0x12);
  wait_busy();
  window();
  g_ready = true;
  ESP_LOGI(kTag,
           "5.79\" e-paper up map=%s SCK=%d MOSI=%d CS=%d DC=%d RST=%d "
           "BUSY=%d PWR=%d busy_now=%d",
           chosen->name, chosen->sck, chosen->mosi, chosen->cs, chosen->dc,
           chosen->rst, chosen->busy, chosen->pwr, busy_level());
  return true;
}

void epd5in79_awaken() {
  if (g_spi == nullptr) {
    return;
  }
  reset_hw();
  wait_busy();
  cmd(0x12);
  wait_busy();
  window();
  g_ready = true;
}

void epd5in79_clear() {
  if (!g_ready) {
    return;
  }
  const int64_t t0 = esp_timer_get_time();
  ESP_LOGI(kTag, "clear begin busy=%d", busy_level());
  constexpr int kHalf = 13600;
  cmd(0x24);
  for (int i = 0; i < kHalf; ++i) {
    data(0xff);
  }
  cmd(0x26);
  for (int i = 0; i < kHalf; ++i) {
    data(0x00);
  }
  cmd(0xa4);
  for (int i = 0; i < kHalf; ++i) {
    data(0xff);
  }
  cmd(0xa6);
  for (int i = 0; i < kHalf; ++i) {
    data(0x00);
  }
  turn_on(0xf7);
  ESP_LOGI(kTag, "clear done %lld ms busy=%d",
           (esp_timer_get_time() - t0) / 1000, busy_level());
}

void epd5in79_display(const uint8_t* frame, bool fast) {
  if (!g_ready || frame == nullptr) {
    return;
  }
  const int64_t t0 = esp_timer_get_time();
  ESP_LOGI(kTag, "display begin fast=%d busy=%d", fast ? 1 : 0, busy_level());
  const int width_ic = (kWidth % 16 == 0) ? (kWidth / 16) : (kWidth / 16 + 1);
  const int stride = kWidth / 8;

  cmd(0x24);
  for (int y = 0; y < kHeight; ++y) {
    data_buf(frame + y * stride, static_cast<size_t>(width_ic));
  }
  cmd(0x26);
  for (int i = 0; i < width_ic * kHeight; ++i) {
    data(0x00);
  }
  cmd(0xa4);
  for (int y = 0; y < kHeight; ++y) {
    data_buf(frame + y * stride + (width_ic - 1),
             static_cast<size_t>(width_ic));
  }
  cmd(0xa6);
  for (int i = 0; i < width_ic * kHeight; ++i) {
    data(0x00);
  }
  // 0xC7 is Waveshare Init_Fast() only (temp register load). Regular
  // 0x12 + window + 0xC7 does not update the glass. Always 0xF7.
  (void)fast;
  turn_on(0xf7);
  ESP_LOGI(kTag, "display done %lld ms busy=%d",
           (esp_timer_get_time() - t0) / 1000, busy_level());
}

void epd5in79_sleep() {
  if (!g_ready) {
    return;
  }
  cmd(0x10);
  data(0x01);
}

}  // namespace halesp
