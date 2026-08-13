#pragma once

// The correlation between the codec's sample clock and the esp_timer
// microsecond domain the rest of the module schedules in.
//
// The DAC runs off its own crystal, so "frame N leaves the jack at time T"
// drifts against esp_timer by tens of ppm. The driver hands us marks —
// (µs, cumulative frames consumed) pairs taken in the DMA completion
// callback — and this class maintains an integer affine map with a
// first-order servo on the rate term. It is the audio path's equivalent of
// Ableton's HostTimeFilter, minus the doubles: core 1 is single-precision
// hardware and the beat math is Q32.32 throughout.

#include <cstdint>

namespace neon {

class SampleClock {
 public:
  // Nominal rate. Also clears the servo state.
  void reset(uint32_t sample_rate);

  // A DMA mark: at t_us the device had consumed `frames` frames since
  // start. Marks arrive once per DMA descriptor (~2.9 ms) and may carry a
  // few hundred µs of interrupt jitter, which the servo filters.
  void update(int64_t t_us, uint64_t frames);

  // When frame `frame` reaches the converter, in the esp_timer domain.
  // Extrapolates freely on both sides of the last mark.
  int64_t us_at_frame(uint64_t frame) const;

  // Inverse of us_at_frame, saturating at 0 below the anchor.
  uint64_t frame_at_us(int64_t t_us) const;

  // Current rate correction against the nominal sample rate, in ppm.
  int32_t ppm() const { return ppm_; }

  // Prediction error of the most recent mark, before it was applied.
  int64_t residual_us() const { return residual_us_; }

  // True once enough marks have been folded in for the rate term to mean
  // something (the caller may want to hold off publishing beat-accurate
  // audio until then; nothing here misbehaves before it).
  bool locked() const { return marks_ >= kLockMarks; }

  uint32_t sample_rate() const { return sample_rate_; }
  uint32_t marks() const { return marks_; }

  // µs per frame, Q32.32, including the ppm correction.
  uint64_t us_per_frame_q32() const { return us_per_frame_q32_; }

 private:
  void apply_ppm();

  // Two loops, deliberately separated.
  //
  // Phase: the anchor moves a sixteenth of the way toward each mark, which
  // averages ~16 marks of interrupt jitter out of the map.
  //
  // Rate: measured directly over a long baseline between two *filtered*
  // anchors, never per-mark. A mark is a whole microsecond and blocks are
  // 2.9 ms apart, so a per-mark rate estimate has ±340 ppm of quantisation
  // noise in it — integrating that is how a servo ends up chasing its own
  // rounding. Over a second the same noise is ±1 ppm.
  static constexpr int64_t kPhaseShift = 4;       // apply residual/16
  static constexpr int32_t kRateDivisor = 4;      // ppm error / 4 per update
  static constexpr int32_t kMaxPpmStep = 20;      // slew limit, ppm per mark
  static constexpr int32_t kMaxPpm = 20000;       // ±2 % is past any crystal
  static constexpr int64_t kOutlierUs = 50000;    // beyond this, re-anchor
  static constexpr int64_t kMinBaselineUs = 200000;    // 0.2 s
  static constexpr int64_t kRebaseUs = 20000000;       // 20 s
  static constexpr uint32_t kBaseMarks = 16;      // let the phase loop settle
  static constexpr uint32_t kLockMarks = 128;

  uint32_t sample_rate_ = 44100;
  uint64_t nominal_us_per_frame_q32_ = 0;
  uint64_t us_per_frame_q32_ = 0;

  bool have_anchor_ = false;
  bool have_base_ = false;
  uint64_t origin_frame_ = 0;
  int64_t origin_us_ = 0;
  uint64_t base_frame_ = 0;
  int64_t base_us_ = 0;
  int64_t last_mark_us_ = 0;
  int64_t residual_us_ = 0;
  int32_t ppm_ = 0;
  uint32_t marks_ = 0;
};

}  // namespace neon
