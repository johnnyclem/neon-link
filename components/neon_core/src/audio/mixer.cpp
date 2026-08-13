#include "neon/audio/mixer.hpp"

namespace neon {

namespace {

// The saturator's job is preventing wrap when several sources sum past
// full scale — it is a safety net, not a stage. A knee at 0.75 put the
// top 2.5 dB of ordinary program material permanently inside the curve,
// and on dense harmonic content (piano, guitar) the resulting
// intermodulation reads as crackly glitching even with a loss-free
// stream. 0.95 keeps the net while leaving normal levels untouched —
// and render_channel skips the pass entirely when the active sources
// cannot exceed full scale in the first place.
constexpr float kKnee = 0.95f;

// Source selection for one output channel. Returns false when the role has
// nothing to render (the caller then writes silence).
struct Tap {
  const float* buf;
  float gain;
};

Tap tap_for(AudioRole role, const MixerConfig& cfg, const MixSources& src,
            bool right) {
  switch (role) {
    case AudioRole::kMetronome:
      return Tap{src.metro, gain_from_byte(cfg.metro_gain)};
    case AudioRole::kClock:
      return Tap{src.clock, 1.0f};
    case AudioRole::kReset:
      return Tap{src.reset, 1.0f};
    case AudioRole::kRun:
      return Tap{src.run, 1.0f};
    case AudioRole::kAmy:
      return Tap{src.amy, gain_from_byte(cfg.amy_gain)};
    case AudioRole::kLinkIn:
      return Tap{right ? src.link_in_r : src.link_in_l,
                 gain_from_byte(cfg.sub_gain)};
    case AudioRole::kLineIn:
      return Tap{right ? src.line_in_r : src.line_in_l,
                 gain_from_byte(cfg.linein_gain)};
    default:
      return Tap{nullptr, 0.0f};
  }
}

// Returns the worst-case sum of the active sources' gains: every source
// is bounded to ±1 before its gain (int16 conversion for link/line, the
// per-voice normalisation in SynthVoiceBank, the click envelope), so a
// bound ≤ 1 proves the mix cannot leave full scale and needs no
// saturation at all.
float render_mix(const MixerConfig& cfg, const MixSources& src,
                 uint32_t frames, bool right, float* out) {
  float bound = 0.0f;
  for (uint32_t i = 0; i < frames; ++i) {
    out[i] = 0.0f;
  }
  if (cfg.metro_enabled && src.metro != nullptr) {
    const float g = gain_from_byte(cfg.metro_gain);
    bound += g;
    for (uint32_t i = 0; i < frames; ++i) {
      out[i] += src.metro[i] * g;
    }
  }
  if (cfg.amy_enabled && src.amy != nullptr) {
    const float g = gain_from_byte(cfg.amy_gain);
    bound += g;
    for (uint32_t i = 0; i < frames; ++i) {
      out[i] += src.amy[i] * g;
    }
  }
  const float* link = right ? src.link_in_r : src.link_in_l;
  if (link != nullptr) {
    const float g = gain_from_byte(cfg.sub_gain);
    bound += g;
    for (uint32_t i = 0; i < frames; ++i) {
      out[i] += link[i] * g;
    }
  }
  const float* line = right ? src.line_in_r : src.line_in_l;
  if (line != nullptr && cfg.linein_gain != 0) {
    const float g = gain_from_byte(cfg.linein_gain);
    bound += g;
    for (uint32_t i = 0; i < frames; ++i) {
      out[i] += line[i] * g;
    }
  }
  return bound;
}

void render_channel(AudioRole role, const MixerConfig& cfg,
                    const MixSources& src, uint32_t frames, bool right,
                    float* out) {
  float bound = 0.0f;
  if (role == AudioRole::kMix) {
    bound = render_mix(cfg, src, frames, right, out);
  } else {
    const Tap t = tap_for(role, cfg, src, right);
    const bool pulse = role == AudioRole::kClock || role == AudioRole::kReset ||
                       role == AudioRole::kRun;
    if (t.buf == nullptr) {
      for (uint32_t i = 0; i < frames; ++i) {
        out[i] = 0.0f;
      }
      return;
    }
    for (uint32_t i = 0; i < frames; ++i) {
      out[i] = t.buf[i] * t.gain;
    }
    if (pulse) {
      // Pulse roles bypass the saturator: a gate must reach the same level
      // every time or a downstream trigger input starts missing edges.
      return;
    }
    bound = t.gain;
  }
  if (bound <= 1.0f) {
    // The active sources cannot sum past full scale, so the saturator
    // has nothing to protect against — stay bit-transparent. This is the
    // common monitoring case: a Link Audio subscription (or line-in)
    // playing on its own at unity gain.
    return;
  }
  for (uint32_t i = 0; i < frames; ++i) {
    out[i] = soft_clip(out[i]);
  }
}

}  // namespace

float soft_clip(float x) {
  const float a = x < 0.0f ? -x : x;
  if (a <= kKnee) {
    return x;
  }
  const float u = (a - kKnee) / (1.0f - kKnee);
  const float y = kKnee + (1.0f - kKnee) * (u / (1.0f + u));
  return x < 0.0f ? -y : y;
}

void mix_block(const MixerConfig& cfg, const MixSources& src, uint32_t frames,
               float* out_l, float* out_r) {
  if (frames == 0) {
    return;
  }
  if (out_l != nullptr) {
    render_channel(cfg.role_l, cfg, src, frames, /*right=*/false, out_l);
  }
  if (out_r != nullptr) {
    render_channel(cfg.role_r, cfg, src, frames, /*right=*/true, out_r);
  }
}

void float_to_int16(const float* l, const float* r, uint32_t frames,
                    int16_t* interleaved) {
  if (interleaved == nullptr) {
    return;
  }
  for (uint32_t i = 0; i < frames; ++i) {
    const float lv = l != nullptr ? l[i] : 0.0f;
    const float rv = r != nullptr ? r[i] : 0.0f;
    float a = lv * 32767.0f;
    float b = rv * 32767.0f;
    if (a > 32767.0f) a = 32767.0f;
    if (a < -32768.0f) a = -32768.0f;
    if (b > 32767.0f) b = 32767.0f;
    if (b < -32768.0f) b = -32768.0f;
    interleaved[2 * i] = static_cast<int16_t>(a);
    interleaved[2 * i + 1] = static_cast<int16_t>(b);
  }
}

void stereo_to_mono_i16(const int16_t* interleaved, uint32_t frames,
                        int16_t* out) {
  if (interleaved == nullptr || out == nullptr) {
    return;
  }
  for (uint32_t i = 0; i < frames; ++i) {
    const int32_t sum = static_cast<int32_t>(interleaved[2 * i]) +
                        static_cast<int32_t>(interleaved[2 * i + 1]);
    out[i] = static_cast<int16_t>(sum / 2);
  }
}

void int16_to_float(const int16_t* interleaved, uint8_t channels,
                    uint32_t frames, float* l, float* r) {
  if (interleaved == nullptr) {
    return;
  }
  constexpr float kScale = 1.0f / 32768.0f;
  if (channels <= 1) {
    for (uint32_t i = 0; i < frames; ++i) {
      const float v = static_cast<float>(interleaved[i]) * kScale;
      if (l != nullptr) l[i] = v;
      if (r != nullptr) r[i] = v;
    }
    return;
  }
  for (uint32_t i = 0; i < frames; ++i) {
    if (l != nullptr) l[i] = static_cast<float>(interleaved[2 * i]) * kScale;
    if (r != nullptr) {
      r[i] = static_cast<float>(interleaved[2 * i + 1]) * kScale;
    }
  }
}

uint16_t peak_meter(const float* buf, uint32_t frames, uint16_t prev) {
  float peak = 0.0f;
  if (buf != nullptr) {
    for (uint32_t i = 0; i < frames; ++i) {
      const float a = buf[i] < 0.0f ? -buf[i] : buf[i];
      if (a > peak) {
        peak = a;
      }
    }
  }
  uint32_t milli = static_cast<uint32_t>(peak * 1000.0f);
  if (milli > 1000u) {
    milli = 1000u;
  }
  // Instant attack, ~7 %/block release: at 2.9 ms blocks a full-scale peak
  // is still visible ~200 ms later, which is what a meter needs to read as
  // a meter rather than a strobe.
  const uint32_t decayed = (static_cast<uint32_t>(prev) * 93u) / 100u;
  return static_cast<uint16_t>(milli > decayed ? milli : decayed);
}

}  // namespace neon
