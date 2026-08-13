#pragma once

// The routing matrix. Each physical output channel carries one AudioRole:
// either the mix (weighted sum of everything enabled) or a solo tap of a
// single source — which is how a jack becomes a sample-accurate clock,
// reset or run output instead of a speaker feed.
//
// float32 internally, int16 at the I2S and network boundaries. The S3's
// FPU is single-precision hardware; only doubles are banned.

#include <cstdint>

#include "neon/audio/types.hpp"

namespace neon {

// Per-block source buffers, all `frames` long and mono unless noted.
// A null pointer means "this source is not producing", which is not the
// same as silence: a null kLinkIn tap leaves the channel quiet without
// costing a pass over a zeroed buffer.
struct MixSources {
  const float* metro = nullptr;
  const float* clock = nullptr;
  const float* reset = nullptr;
  const float* run = nullptr;
  const float* amy = nullptr;
  const float* link_in_l = nullptr;
  const float* link_in_r = nullptr;
  const float* line_in_l = nullptr;
  const float* line_in_r = nullptr;
};

struct MixerConfig {
  AudioRole role_l = AudioRole::kMix;
  AudioRole role_r = AudioRole::kMix;
  uint8_t metro_gain = kUnityGainByte;
  uint8_t amy_gain = kUnityGainByte;
  uint8_t linein_gain = 0;
  uint8_t sub_gain = kUnityGainByte;
  bool metro_enabled = false;
  bool amy_enabled = false;
  // Pulse roles are DC-ish gates: full scale is a 0/+1 step, and they are
  // never folded into kMix (a clock in the monitor mix is a mistake, not a
  // feature).
  float pulse_level = 0.9f;
};

// Renders the two output channels. out_l / out_r are overwritten.
void mix_block(const MixerConfig& cfg, const MixSources& src, uint32_t frames,
               float* out_l, float* out_r);

// Cubic-free soft saturator: unity slope below the knee, asymptotic to
// ±1 above it. Monotone, so it can never wrap the way a bare cast would.
float soft_clip(float x);

// Interleaves and converts to int16 with saturation.
void float_to_int16(const float* l, const float* r, uint32_t frames,
                    int16_t* interleaved);

// Sums a stereo interleaved int16 block to mono in place-compatible form
// (out has `frames` samples). Used by the mono publish option.
void stereo_to_mono_i16(const int16_t* interleaved, uint32_t frames,
                        int16_t* out);

// De-interleaves int16 into float ±1 channels. `channels` may be 1 (both
// outputs get the same signal) or 2.
void int16_to_float(const int16_t* interleaved, uint8_t channels,
                    uint32_t frames, float* l, float* r);

// Peak of a block, as 0..1000 milli-full-scale, with a decay applied to
// `prev` so the meter falls back instead of latching.
uint16_t peak_meter(const float* buf, uint32_t frames, uint16_t prev);

}  // namespace neon
