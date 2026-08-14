#pragma once

// Link-in "gist" filter: a 120 Hz–5 kHz band. The high-pass is 4-pole so
// the 25–55 Hz band (where packet splices read as the worst crackle) is
// actually gone, not just softened. The 5 kHz low-pass still dumps the
// overtone forest that linear SRC turns into digital junk.
//
// float32 only — the S3 FPU is single-precision. Two GistFilter instances
// cover stereo.

#include <cstdint>

namespace neon {

inline constexpr float kGistLowCutHz = 120.0f;
inline constexpr float kGistHighCutHz = 5000.0f;
inline constexpr float kGistQ = 0.70710678f;  // Butterworth section

// Kept so existing call sites and tests that name the old LPF still compile.
inline constexpr float kGistCutoffHz = kGistHighCutHz;

enum class BiquadKind : uint8_t { kLowpass = 0, kHighpass = 1 };

class Biquad {
 public:
  void reset();
  void set(BiquadKind kind, uint32_t sample_rate, float cutoff_hz, float q);
  void set_lowpass(uint32_t sample_rate, float cutoff_hz, float q) {
    set(BiquadKind::kLowpass, sample_rate, cutoff_hz, q);
  }
  void set_highpass(uint32_t sample_rate, float cutoff_hz, float q) {
    set(BiquadKind::kHighpass, sample_rate, cutoff_hz, q);
  }

  // In-place. Null or empty is a no-op.
  void process(float* x, uint32_t frames);

  float b0() const { return b0_; }
  float b1() const { return b1_; }
  float b2() const { return b2_; }
  float a1() const { return a1_; }
  float a2() const { return a2_; }

 private:
  float b0_ = 1.0f;
  float b1_ = 0.0f;
  float b2_ = 0.0f;
  float a1_ = 0.0f;
  float a2_ = 0.0f;
  float z1_ = 0.0f;
  float z2_ = 0.0f;
};

using BiquadLowpass = Biquad;

// One channel of the jam-monitor band: HPF → HPF → LPF → deglitch.
class GistFilter {
 public:
  void reset();
  void set_rate(uint32_t sample_rate);
  void process(float* x, uint32_t frames);

 private:
  void deglitch(float* x, uint32_t frames);

  Biquad hp1_;
  Biquad hp2_;
  Biquad lp_;
  float prev_ = 0.0f;
};

}  // namespace neon
