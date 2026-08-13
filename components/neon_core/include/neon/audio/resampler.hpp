#pragma once

// Linear-interpolation sample-rate conversion for the receive path.
//
// Live publishes at 48 kHz and so does the module, but senders are
// allowed any rate and the two crystals are not the same. Linear
// interpolation is coarse for mastering and entirely adequate for
// monitoring and jamming, which is
// what a Link Audio subscription is for — and it costs two multiplies per
// output sample. A windowed-sinc upgrade slots in behind this interface
// without touching the callers.
//
// The step is Q32.32 and carries a ±ppm trim, which is where the receive
// buffer's fill-level servo steers.

#include <cstdint>

namespace neon {

class LinearResampler {
 public:
  void reset();
  void set_rates(uint32_t in_rate, uint32_t out_rate);
  void set_trim_ppm(int32_t ppm);

  uint32_t in_rate() const { return in_rate_; }
  uint32_t out_rate() const { return out_rate_; }
  int32_t trim_ppm() const { return trim_ppm_; }
  uint64_t step_q32() const { return step_q32_; }

  // Consumes from `in` (stereo interleaved int16) and writes up to
  // out_frames into l/r as float ±1. Returns the number of output frames
  // produced; *consumed receives the number of input frames the caller may
  // now discard (the remainder must be presented again next call).
  uint32_t process(const int16_t* in, uint32_t in_frames, uint32_t* consumed,
                   float* l, float* r, uint32_t out_frames);

  static constexpr int32_t kMaxTrimPpm = 500;

 private:
  void recompute();

  uint32_t in_rate_ = 44100;
  uint32_t out_rate_ = 44100;
  int32_t trim_ppm_ = 0;
  uint64_t step_q32_ = 1ull << 32;
  uint64_t pos_q32_ = 0;
};

}  // namespace neon
