#pragma once

// I2S duplex audio for the AMYboard's PCM5101 (out) and PCM1808 (in).
//
// One I2S port in duplex mode: TX and RX share BCLK and WS, which is the
// only arrangement that keeps the two converters on one sample clock — and
// therefore keeps one SampleClock instance valid for both directions. If a
// board wires the ADC to its own crystal instead, the input side needs its
// own instance; see docs/AUDIOLINK.md.
//
// The on_sent callback records (esp_timer µs, cumulative frames) marks
// through an ISR-safe seqlock, which is what SampleClock is fed with.

#include <atomic>
#include <cstdint>

#include "hal/IAudioIo.hpp"

namespace halesp {

struct I2sPins {
  int mclk = -1;
  int bclk = -1;
  int ws = -1;
  int dout = -1;
  int din = -1;
};

class I2sAudio : public hal::IAudioIo {
 public:
  // Pins come from board_pins.h; a -1 on bclk/ws/dout disables the driver
  // entirely (start() then returns false and the caller stays silent).
  void set_pins(const I2sPins& pins) { pins_ = pins; }

  bool start(const hal::AudioIoConfig& cfg) override;
  void stop() override;
  bool running() const override { return running_; }

  bool write_block(const int16_t* interleaved) override;
  bool read_block(int16_t* interleaved) override;
  bool dma_mark(int64_t& t_us, uint64_t& frames_consumed) override;
  uint64_t frames_written() const override { return frames_written_; }
  int32_t output_latency_us() const override { return latency_us_; }

  // Counters for the status document.
  uint32_t write_failures() const { return write_failures_; }
  uint32_t read_failures() const { return read_failures_; }

  // ISR entry point. Public because the IDF callback is a plain function.
  void on_dma_sent(uint32_t bytes);

 private:
  I2sPins pins_{};
  hal::AudioIoConfig cfg_{};
  bool running_ = false;
  bool input_running_ = false;
  int32_t latency_us_ = 0;
  uint64_t frames_written_ = 0;
  uint32_t write_failures_ = 0;
  uint32_t read_failures_ = 0;

  // ISR-side mark, published with an odd/even sequence counter.
  std::atomic<uint32_t> mark_seq_{0};
  std::atomic<uint32_t> mark_generation_{0};
  volatile int64_t mark_us_ = 0;
  volatile uint64_t mark_frames_ = 0;
  uint64_t isr_frames_ = 0;
  uint32_t last_seen_generation_ = 0;
};

// The process-wide instance the audio service drives.
I2sAudio& i2s_audio();

}  // namespace halesp
