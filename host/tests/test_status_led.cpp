#include <doctest.h>

#include "neon/fixed_math.hpp"
#include "neon/status_led.hpp"

TEST_CASE("LED pattern priority: unprovisioned / connecting / play / peers") {
  CHECK(neon::classify_led(false, false, 0, false) ==
        neon::LedPattern::Unprovisioned);
  CHECK(neon::classify_led(false, true, 2, true) ==
        neon::LedPattern::Unprovisioned);
  CHECK(neon::classify_led(true, false, 0, false) ==
        neon::LedPattern::Connecting);
  CHECK(neon::classify_led(true, true, 0, false) ==
        neon::LedPattern::LinkNoPeers);
  CHECK(neon::classify_led(true, true, 3, false) ==
        neon::LedPattern::LinkStopped);
  CHECK(neon::classify_led(true, true, 0, true) == neon::LedPattern::Playing);
  CHECK(neon::classify_led(true, true, 3, true) == neon::LedPattern::Playing);
}

TEST_CASE("breathe is a 2 s triangle that hits both rails") {
  neon::TimelineSnapshot tl{};
  CHECK(neon::led_duty(neon::LedPattern::Unprovisioned, tl, 0) == 0);
  CHECK(neon::led_duty(neon::LedPattern::Unprovisioned, tl, 1000000) == 255);
  CHECK(neon::led_duty(neon::LedPattern::Unprovisioned, tl, 2000000) == 0);
  const uint8_t mid = neon::led_duty(neon::LedPattern::Unprovisioned, tl, 500000);
  CHECK(mid > 100);
  CHECK(mid < 160);
}

TEST_CASE("connecting blinks at 5 Hz") {
  neon::TimelineSnapshot tl{};
  CHECK(neon::led_duty(neon::LedPattern::Connecting, tl, 0) == 255);
  CHECK(neon::led_duty(neon::LedPattern::Connecting, tl, 99000) == 255);
  CHECK(neon::led_duty(neon::LedPattern::Connecting, tl, 100000) == 0);
  CHECK(neon::led_duty(neon::LedPattern::Connecting, tl, 199000) == 0);
  CHECK(neon::led_duty(neon::LedPattern::Connecting, tl, 200000) == 255);
}

TEST_CASE("no-peers is a double blink every 2 s") {
  neon::TimelineSnapshot tl{};
  CHECK(neon::led_duty(neon::LedPattern::LinkNoPeers, tl, 0) == 255);
  CHECK(neon::led_duty(neon::LedPattern::LinkNoPeers, tl, 79000) == 255);
  CHECK(neon::led_duty(neon::LedPattern::LinkNoPeers, tl, 100000) == 0);
  CHECK(neon::led_duty(neon::LedPattern::LinkNoPeers, tl, 180000) == 255);
  CHECK(neon::led_duty(neon::LedPattern::LinkNoPeers, tl, 300000) == 0);
  CHECK(neon::led_duty(neon::LedPattern::LinkNoPeers, tl, 2000000) == 255);
}

TEST_CASE("stopped with peers is solid; playing flashes the downbeat only") {
  neon::TimelineSnapshot tl{};
  CHECK(neon::led_duty(neon::LedPattern::LinkStopped, tl, 123456) == 255);

  tl.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(120000);
  tl.origin_us = 0;
  tl.beat_at_origin_q32 = 0;
  tl.quantum_beats = 4;
  tl.playing = 1;
  // t=0 is the downbeat; t=100 ms is well into beat 1 (500 ms/beat).
  CHECK(neon::led_duty(neon::LedPattern::Playing, tl, 0) == 255);
  CHECK(neon::led_duty(neon::LedPattern::Playing, tl, 100000) == 0);
  // Next bar at 2 s.
  CHECK(neon::led_duty(neon::LedPattern::Playing, tl, 2000000) == 255);
  CHECK(neon::led_duty(neon::LedPattern::Playing, tl, 2100000) == 0);
}
