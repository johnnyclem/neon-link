#pragma once

// Shared audio vocabulary: what an output channel carries, what the
// metronome sounds like, and the live-applied slice of the configuration
// the audio task reads through a seqlock. Kept separate from
// config/model.hpp so the DSP headers do not pull the whole config in.

#include <cstdint>

namespace neon {

// What one physical output channel (L or R) carries. kMix is the weighted
// sum of every enabled source; the rest are solo taps, which is how the
// pulse roles deliver sample-accurate clock/reset/run as audio.
enum class AudioRole : uint8_t {
  kMix = 0,
  kMetronome = 1,
  kClock = 2,
  kReset = 3,
  kRun = 4,
  kAmy = 5,
  kLinkIn = 6,
  kLineIn = 7,
  kRoleCount = 8,
};

enum class ClickSound : uint8_t {
  kSine = 0,
  kNoise = 1,
  kWood = 2,
  kSoundCount = 3,
};

// Gain bytes are 0..255 with 200 = unity, so the UI can push a little
// past 0 dB (255 ≈ +2.1 dB) without a separate sign convention.
inline constexpr uint8_t kUnityGainByte = 200;

inline float gain_from_byte(uint8_t g) {
  return static_cast<float>(g) / static_cast<float>(kUnityGainByte);
}

// The live-applied configuration slice: written by whoever changes the
// config (web / OLED / REST), read by the audio task once per block.
// Restart-scope fields (audio_enabled, publish flags, subscription) are
// handled core-0-side and are deliberately not in here.
struct AudioEngineConfig {
  uint8_t enabled = 0;
  AudioRole role_l = AudioRole::kMix;
  AudioRole role_r = AudioRole::kMix;
  uint8_t metro_enabled = 0;

  ClickSound metro_sound = ClickSound::kSine;
  uint8_t metro_gain = kUnityGainByte;
  uint8_t metro_accent = 1;
  uint8_t amy_enabled = 0;

  uint8_t amy_gain = kUnityGainByte;
  uint8_t amy_patch = 0;
  uint8_t linein_monitor_gain = 0;
  uint8_t la_sub_gain = kUnityGainByte;

  uint8_t la_publish_mono = 0;
  uint8_t la_fullband = 0;  // 0 = apply the 120 Hz–5 kHz gist band on link-in
  uint16_t la_jitter_ms = 60;

  uint32_t quantum_beats = 4;
};

// Status published back to the UI (web / OLED) by the audio task.
struct AudioStatus {
  uint8_t running = 0;
  uint8_t publishing = 0;   // at least one sink has a subscriber
  uint8_t sub_state = 0;    // 0 idle, 1 buffering, 2 playing
  uint8_t pad_[1] = {};

  uint16_t peak_l = 0;  // 0..1000 (milli-full-scale), decaying
  uint16_t peak_r = 0;

  uint32_t underruns = 0;   // I2S write failures / starved blocks
  uint32_t sub_dropped = 0; // frames dropped inside the jitter ring
  uint32_t sub_rate = 0;    // sender sample rate, 0 when not receiving
  uint32_t subscribers = 0; // peers listening to our published channels
  uint32_t fill_ms = 0;     // receive-buffer fill, milliseconds
  int32_t clock_ppm = 0;    // SampleClock rate correction
  int32_t clock_residual_us = 0;

  // Each stage of the receive/publish path loses audio for a different
  // reason, and lumping them into one counter is what made the on-device
  // crackle undiagnosable. Kept separate so a log line or /api/status can
  // say *which* stage is bleeding.
  uint32_t rx_dropped = 0;     // blocks lost network→audio ring (pre-jitter)
  uint32_t jit_underruns = 0;  // jitter-buffer rebuffer events
  uint32_t tx_dropped = 0;     // publish blocks lost audio→pump ring
  int32_t trim_ppm = 0;        // receive resampler servo trim
  uint32_t concealed = 0;      // beat-gap splices faded instead of hard-cut
};

}  // namespace neon
