#include "neon/status_led.hpp"

namespace neon {

LedPattern classify_led(bool provisioned, bool wifi_up, uint32_t peers,
                        bool playing) {
  if (!provisioned) {
    return LedPattern::Unprovisioned;
  }
  if (!wifi_up) {
    return LedPattern::Connecting;
  }
  if (playing) {
    return LedPattern::Playing;
  }
  if (peers == 0) {
    return LedPattern::LinkNoPeers;
  }
  return LedPattern::LinkStopped;
}

uint8_t led_duty(LedPattern pattern, const TimelineSnapshot& tl,
                 int64_t now_us) {
  if (now_us < 0) {
    now_us = 0;
  }
  const uint64_t t = static_cast<uint64_t>(now_us);

  switch (pattern) {
    case LedPattern::Unprovisioned: {
      // Triangle 0→255→0 over 2 s.
      const uint32_t phase = static_cast<uint32_t>(t % 2000000ull);
      if (phase < 1000000u) {
        return static_cast<uint8_t>(phase * 255u / 1000000u);
      }
      return static_cast<uint8_t>((2000000u - phase) * 255u / 1000000u);
    }
    case LedPattern::Connecting:
      // 5 Hz, 50% duty: 100 ms on, 100 ms off.
      return ((t / 100000ull) % 2ull) == 0 ? 255 : 0;
    case LedPattern::LinkNoPeers: {
      // Two 80 ms flashes at the start of each 2 s window.
      const uint32_t phase = static_cast<uint32_t>(t % 2000000ull);
      if (phase < 80000u || (phase >= 160000u && phase < 240000u)) {
        return 255;
      }
      return 0;
    }
    case LedPattern::LinkStopped:
      return 255;
    case LedPattern::Playing: {
      if (tl.tempo_mpb_q32 == 0 || tl.playing == 0) {
        return 0;
      }
      // First ~80 milli-beats of the bar (≈ 40 ms at 120 BPM).
      const uint32_t phase = phase_milli_beats(tl, now_us);
      return phase < 80u ? 255 : 0;
    }
  }
  return 0;
}

}  // namespace neon
