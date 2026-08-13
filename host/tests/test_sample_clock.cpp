#include <doctest.h>

#include <cstdint>

#include "neon/audio/sample_clock.hpp"

namespace {

// Deterministic ±jitter, so a failure is reproducible.
int64_t jitter(uint32_t& seed, int64_t amplitude_us) {
  seed = seed * 1664525u + 1013904223u;
  const int64_t span = 2 * amplitude_us + 1;
  return static_cast<int64_t>(seed % static_cast<uint32_t>(span)) - amplitude_us;
}

// Drives the clock with a device whose real rate is `rate_ppm` off nominal.
// Returns the absolute prediction error, in µs, of the final mark.
int64_t run_drift(neon::SampleClock& clk, int32_t rate_ppm, int marks,
                  int64_t jitter_us, uint32_t seed = 1) {
  constexpr uint32_t kRate = 44100;
  constexpr uint64_t kBlock = 128;
  clk.reset(kRate);
  int64_t worst = 0;
  for (int i = 0; i < marks; ++i) {
    const uint64_t frames = kBlock * static_cast<uint64_t>(i);
    // True time of this frame: nominal period stretched by the ppm error.
    const double nominal_us =
        static_cast<double>(frames) * 1000000.0 / static_cast<double>(kRate);
    const int64_t truth =
        static_cast<int64_t>(nominal_us * (1.0 + rate_ppm / 1000000.0));
    const int64_t observed = truth + jitter(seed, jitter_us);
    if (i > marks / 2) {
      const int64_t err = clk.us_at_frame(frames) - truth;
      worst = err < 0 ? -err : err;
    }
    clk.update(observed, frames);
  }
  return worst;
}

}  // namespace

TEST_CASE("SampleClock: nominal map before any correction") {
  neon::SampleClock clk;
  clk.reset(44100);
  clk.update(1000000, 0);
  // One second of frames is one second of microseconds, to within the
  // Q32.32 rounding of 1e6/44100.
  const int64_t t = clk.us_at_frame(44100);
  CHECK(t >= 1999999);
  CHECK(t <= 2000001);
  CHECK(clk.ppm() == 0);
  CHECK_FALSE(clk.locked());
}

TEST_CASE("SampleClock: converges on +80 ppm drift to under a sample") {
  neon::SampleClock clk;
  // 2000 marks is ~6 s of audio at 128 frames / 44.1 kHz.
  const int64_t err = run_drift(clk, 80, 2000, /*jitter_us=*/0);
  CHECK(err < 23);  // one sample period at 44.1 kHz
  CHECK(clk.ppm() > 60);
  CHECK(clk.ppm() < 100);
  CHECK(clk.locked());
}

TEST_CASE("SampleClock: negative drift converges too") {
  neon::SampleClock clk;
  const int64_t err = run_drift(clk, -50, 2000, /*jitter_us=*/0);
  CHECK(err < 23);
  CHECK(clk.ppm() < -30);
  CHECK(clk.ppm() > -70);
}

TEST_CASE("SampleClock: rejects +/-200 us of mark jitter") {
  neon::SampleClock clk;
  // The servo must not chase interrupt jitter: the rate estimate stays
  // near zero and the prediction stays inside a few samples.
  const int64_t err = run_drift(clk, 0, 3000, /*jitter_us=*/200, /*seed=*/7);
  CHECK(err < 200);
  CHECK(clk.ppm() < 40);
  CHECK(clk.ppm() > -40);
}

TEST_CASE("SampleClock: rate slews, it does not jump") {
  neon::SampleClock clk;
  clk.reset(44100);
  clk.update(0, 0);
  // A mark 1 ms late after one block would imply ~350000 ppm; the slew
  // limit lets through 20.
  clk.update(2900 + 1000, 128);
  CHECK(clk.ppm() <= 20);
  CHECK(clk.ppm() >= -20);
}

TEST_CASE("SampleClock: an absurd mark re-anchors instead of steering") {
  neon::SampleClock clk;
  clk.reset(44100);
  for (int i = 0; i < 200; ++i) {
    clk.update(static_cast<int64_t>(i) * 2902, 128ull * i);
  }
  const int32_t before = clk.ppm();
  // Half a second of stall, e.g. a driver restart.
  clk.update(200 * 2902 + 500000, 128ull * 200);
  CHECK(clk.ppm() == before);
  // The map follows the new anchor exactly.
  CHECK(clk.us_at_frame(128ull * 200) == 200 * 2902 + 500000);
}

TEST_CASE("SampleClock: frame_at_us inverts us_at_frame") {
  neon::SampleClock clk;
  run_drift(clk, 60, 500, 0);
  for (uint64_t f = 0; f < 44100 * 4; f += 7919) {
    const int64_t t = clk.us_at_frame(f);
    const uint64_t back = clk.frame_at_us(t);
    const int64_t d = static_cast<int64_t>(back) - static_cast<int64_t>(f);
    CHECK(d <= 1);
    CHECK(d >= -1);
  }
}

TEST_CASE("SampleClock: below the anchor saturates at frame zero") {
  neon::SampleClock clk;
  clk.reset(48000);
  clk.update(1000000, 0);
  CHECK(clk.frame_at_us(0) == 0);
  CHECK(clk.us_at_frame(0) == 1000000);
}
