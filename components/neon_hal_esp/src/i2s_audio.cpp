#include "halesp/i2s_audio.hpp"

#include <cstring>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace halesp {

namespace {

const char* kTag = "i2s_audio";

i2s_chan_handle_t g_tx = nullptr;
i2s_chan_handle_t g_rx = nullptr;

// IRAM: this fires from the I2S DMA interrupt, once per descriptor.
IRAM_ATTR bool on_sent_isr(i2s_chan_handle_t, i2s_event_data_t* event,
                           void* user) {
  auto* self = static_cast<I2sAudio*>(user);
  if (self != nullptr && event != nullptr) {
    self->on_dma_sent(static_cast<uint32_t>(event->size));
  }
  return false;  // no task woken
}

}  // namespace

void I2sAudio::on_dma_sent(uint32_t bytes) {
  // 4 bytes per stereo int16 frame.
  isr_frames_ += bytes / 4u;
  const uint32_t s = mark_seq_.load(std::memory_order_relaxed);
  mark_seq_.store(s + 1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  mark_us_ = esp_timer_get_time();
  mark_frames_ = isr_frames_;
  std::atomic_thread_fence(std::memory_order_release);
  mark_seq_.store(s + 2, std::memory_order_relaxed);
  mark_generation_.fetch_add(1, std::memory_order_release);
}

bool I2sAudio::dma_mark(int64_t& t_us, uint64_t& frames_consumed) {
  const uint32_t gen = mark_generation_.load(std::memory_order_acquire);
  if (gen == last_seen_generation_) {
    return false;  // nothing new since the caller last asked
  }
  for (int attempt = 0; attempt < 4; ++attempt) {
    const uint32_t s1 = mark_seq_.load(std::memory_order_acquire);
    if (s1 & 1u) {
      continue;
    }
    const int64_t us = mark_us_;
    const uint64_t frames = mark_frames_;
    std::atomic_thread_fence(std::memory_order_acquire);
    if (mark_seq_.load(std::memory_order_acquire) == s1) {
      t_us = us;
      frames_consumed = frames;
      last_seen_generation_ = gen;
      return true;
    }
  }
  return false;
}

bool I2sAudio::start(const hal::AudioIoConfig& cfg) {
  if (running_) {
    return true;
  }
  if (pins_.bclk < 0 || pins_.ws < 0 || pins_.dout < 0) {
    ESP_LOGW(kTag, "no I2S pins for this board profile; audio disabled");
    return false;
  }
  cfg_ = cfg;
  isr_frames_ = 0;
  frames_written_ = 0;
  write_failures_ = 0;
  read_failures_ = 0;
  last_seen_generation_ = 0;
  mark_generation_.store(0, std::memory_order_relaxed);

  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = cfg.dma_desc;
  chan_cfg.dma_frame_num = cfg.block_frames;
  chan_cfg.auto_clear = true;  // a starved DMA emits silence, not the last block

  const bool want_input = cfg.enable_input && pins_.din >= 0;
  esp_err_t err = i2s_new_channel(&chan_cfg, &g_tx, want_input ? &g_rx : nullptr);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "i2s_new_channel: %s", esp_err_to_name(err));
    return false;
  }

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg.sample_rate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = static_cast<gpio_num_t>(pins_.mclk),
              .bclk = static_cast<gpio_num_t>(pins_.bclk),
              .ws = static_cast<gpio_num_t>(pins_.ws),
              .dout = static_cast<gpio_num_t>(pins_.dout),
              .din = want_input ? static_cast<gpio_num_t>(pins_.din)
                                : I2S_GPIO_UNUSED,
              .invert_flags = {false, false, false},
          },
  };
  if (pins_.mclk < 0) {
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  } else {
    // The PCM1808 wants 256fs; the PCM5101 is happy with it too, so one
    // MCLK serves both sides of the duplex pair.
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  }

  err = i2s_channel_init_std_mode(g_tx, &std_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "i2s TX init: %s", esp_err_to_name(err));
    stop();
    return false;
  }
  if (want_input) {
    err = i2s_channel_init_std_mode(g_rx, &std_cfg);
    if (err != ESP_OK) {
      ESP_LOGW(kTag, "i2s RX init: %s (continuing output-only)",
               esp_err_to_name(err));
      i2s_del_channel(g_rx);
      g_rx = nullptr;
    }
  }

  const i2s_event_callbacks_t cbs = {
      .on_recv = nullptr,
      .on_recv_q_ovf = nullptr,
      .on_sent = on_sent_isr,
      .on_send_q_ovf = nullptr,
  };
  err = i2s_channel_register_event_callback(g_tx, &cbs, this);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "DMA marks unavailable: %s", esp_err_to_name(err));
  }

  err = i2s_channel_enable(g_tx);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "i2s TX enable: %s", esp_err_to_name(err));
    stop();
    return false;
  }
  if (g_rx != nullptr) {
    err = i2s_channel_enable(g_rx);
    input_running_ = err == ESP_OK;
    if (!input_running_) {
      ESP_LOGW(kTag, "i2s RX enable: %s", esp_err_to_name(err));
    }
  }

  // Worst case the DAC is a whole DMA ring behind what we just handed it.
  latency_us_ = static_cast<int32_t>(
      (static_cast<int64_t>(cfg.block_frames) * cfg.dma_desc * 1000000) /
      cfg.sample_rate);
  running_ = true;
  ESP_LOGI(kTag, "I2S up: %lu Hz, %u frames x %u desc, in=%d, latency=%ld us",
           static_cast<unsigned long>(cfg.sample_rate),
           static_cast<unsigned>(cfg.block_frames),
           static_cast<unsigned>(cfg.dma_desc), input_running_ ? 1 : 0,
           static_cast<long>(latency_us_));
  return true;
}

void I2sAudio::stop() {
  if (g_tx != nullptr) {
    i2s_channel_disable(g_tx);
    i2s_del_channel(g_tx);
    g_tx = nullptr;
  }
  if (g_rx != nullptr) {
    i2s_channel_disable(g_rx);
    i2s_del_channel(g_rx);
    g_rx = nullptr;
  }
  running_ = false;
  input_running_ = false;
}

bool I2sAudio::write_block(const int16_t* interleaved) {
  if (!running_ || g_tx == nullptr || interleaved == nullptr) {
    return false;
  }
  const size_t bytes = static_cast<size_t>(cfg_.block_frames) * 4u;
  size_t written = 0;
  // The timeout is the whole DMA ring: if the driver cannot take a block in
  // that long, something has gone wrong upstream and the caller should
  // count an underrun rather than block the task forever.
  const uint32_t timeout_ms =
      1 + (1000u * cfg_.block_frames * cfg_.dma_desc) / cfg_.sample_rate;
  const esp_err_t err = i2s_channel_write(g_tx, interleaved, bytes, &written,
                                          pdMS_TO_TICKS(timeout_ms));
  if (err != ESP_OK || written != bytes) {
    ++write_failures_;
    return false;
  }
  frames_written_ += cfg_.block_frames;
  return true;
}

bool I2sAudio::read_block(int16_t* interleaved) {
  if (!input_running_ || g_rx == nullptr || interleaved == nullptr) {
    return false;
  }
  const size_t bytes = static_cast<size_t>(cfg_.block_frames) * 4u;
  size_t got = 0;
  const esp_err_t err = i2s_channel_read(g_rx, interleaved, bytes, &got, 0);
  if (err != ESP_OK || got != bytes) {
    if (err != ESP_ERR_TIMEOUT) {
      ++read_failures_;
    }
    return false;
  }
  return true;
}

I2sAudio& i2s_audio() {
  static I2sAudio instance;
  return instance;
}

}  // namespace halesp
