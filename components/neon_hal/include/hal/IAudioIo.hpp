#pragma once

// The audio device seam, mirroring ILinkSession: the portable engine talks
// to this, the ESP driver implements it, and the host tests stand in for it
// with a fake that returns synthetic DMA marks.
//
// write_block() blocking on DMA space is the pacing mechanism for the whole
// audio task — there is no timer and no sleep in the render loop.

#include <cstdint>

namespace hal {

struct AudioIoConfig {
  uint32_t sample_rate = 44100;
  uint16_t block_frames = 128;
  uint8_t dma_desc = 4;
  bool enable_input = false;
};

class IAudioIo {
 public:
  virtual ~IAudioIo() = default;

  virtual bool start(const AudioIoConfig& cfg) = 0;
  virtual void stop() = 0;
  virtual bool running() const = 0;

  // One block of interleaved stereo int16. Blocks until the DMA has room,
  // which is what paces the render loop. Returns false on a driver error
  // or a timeout (the caller counts that as an underrun).
  virtual bool write_block(const int16_t* interleaved) = 0;

  // One block of input, non-blocking. Returns false when the input side is
  // disabled or has nothing ready; `interleaved` is left untouched.
  virtual bool read_block(int16_t* interleaved) = 0;

  // The most recent (esp_timer µs, cumulative frames consumed) pair taken
  // in the DMA completion callback. Returns false until one exists, or
  // when the caller has already seen this one.
  virtual bool dma_mark(int64_t& t_us, uint64_t& frames_consumed) = 0;

  // Frames handed to the driver since start().
  virtual uint64_t frames_written() const = 0;

  // Output latency between write_block() returning and the frames reaching
  // the converter, in µs. Used to place the block on the beat grid.
  virtual int32_t output_latency_us() const = 0;
};

}  // namespace hal
