#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "neon/audio/mixer.hpp"

namespace {

constexpr uint32_t kFrames = 64;

std::vector<float> constant(float v) { return std::vector<float>(kFrames, v); }

}  // namespace

TEST_CASE("mixer: kMix sums the enabled sources only") {
  const auto metro = constant(0.2f);
  const auto amy = constant(0.1f);
  neon::MixSources src;
  src.metro = metro.data();
  src.amy = amy.data();

  neon::MixerConfig cfg;
  cfg.metro_enabled = true;
  cfg.amy_enabled = false;
  cfg.metro_gain = neon::kUnityGainByte;
  cfg.amy_gain = neon::kUnityGainByte;

  std::vector<float> l(kFrames), r(kFrames);
  neon::mix_block(cfg, src, kFrames, l.data(), r.data());
  CHECK(l[0] == doctest::Approx(0.2f));
  CHECK(r[0] == doctest::Approx(0.2f));

  cfg.amy_enabled = true;
  neon::mix_block(cfg, src, kFrames, l.data(), r.data());
  CHECK(l[0] == doctest::Approx(0.3f));
}

TEST_CASE("mixer: gains are relative to the unity byte") {
  const auto metro = constant(0.5f);
  neon::MixSources src;
  src.metro = metro.data();
  neon::MixerConfig cfg;
  cfg.metro_enabled = true;
  cfg.metro_gain = neon::kUnityGainByte / 2;

  std::vector<float> l(kFrames), r(kFrames);
  neon::mix_block(cfg, src, kFrames, l.data(), r.data());
  CHECK(l[0] == doctest::Approx(0.25f));

  cfg.metro_gain = 0;
  neon::mix_block(cfg, src, kFrames, l.data(), r.data());
  CHECK(l[0] == doctest::Approx(0.0f));
}

TEST_CASE("mixer: a solo role ignores everything else") {
  const auto metro = constant(0.5f);
  const auto clock = constant(1.0f);
  neon::MixSources src;
  src.metro = metro.data();
  src.clock = clock.data();

  neon::MixerConfig cfg;
  cfg.metro_enabled = true;
  cfg.role_l = neon::AudioRole::kClock;
  cfg.role_r = neon::AudioRole::kMix;

  std::vector<float> l(kFrames), r(kFrames);
  neon::mix_block(cfg, src, kFrames, l.data(), r.data());
  CHECK(l[0] == doctest::Approx(1.0f));  // pulse role bypasses the saturator
  CHECK(r[0] == doctest::Approx(0.5f));
}

TEST_CASE("mixer: line in and link in take their own sides") {
  const auto ll = constant(0.4f);
  const auto lr = constant(-0.4f);
  neon::MixSources src;
  src.line_in_l = ll.data();
  src.line_in_r = lr.data();

  neon::MixerConfig cfg;
  cfg.role_l = neon::AudioRole::kLineIn;
  cfg.role_r = neon::AudioRole::kLineIn;
  cfg.linein_gain = neon::kUnityGainByte;

  std::vector<float> l(kFrames), r(kFrames);
  neon::mix_block(cfg, src, kFrames, l.data(), r.data());
  CHECK(l[0] == doctest::Approx(0.4f));
  CHECK(r[0] == doctest::Approx(-0.4f));
}

TEST_CASE("mixer: a role with no source is silence, not garbage") {
  neon::MixSources src;  // everything null
  neon::MixerConfig cfg;
  cfg.role_l = neon::AudioRole::kAmy;
  cfg.role_r = neon::AudioRole::kLinkIn;
  std::vector<float> l(kFrames, 9.0f), r(kFrames, 9.0f);
  neon::mix_block(cfg, src, kFrames, l.data(), r.data());
  for (uint32_t i = 0; i < kFrames; ++i) {
    CHECK(l[i] == 0.0f);
    CHECK(r[i] == 0.0f);
  }
}

TEST_CASE("mixer: silence in, silence out") {
  const auto zero = constant(0.0f);
  neon::MixSources src;
  src.metro = zero.data();
  src.amy = zero.data();
  src.line_in_l = zero.data();
  src.line_in_r = zero.data();
  neon::MixerConfig cfg;
  cfg.metro_enabled = true;
  cfg.amy_enabled = true;
  cfg.linein_gain = neon::kUnityGainByte;
  std::vector<float> l(kFrames), r(kFrames);
  neon::mix_block(cfg, src, kFrames, l.data(), r.data());
  for (uint32_t i = 0; i < kFrames; ++i) {
    CHECK(l[i] == 0.0f);
    CHECK(r[i] == 0.0f);
  }
}

TEST_CASE("soft_clip: monotone, unity below the knee, bounded above it") {
  CHECK(neon::soft_clip(0.0f) == 0.0f);
  CHECK(neon::soft_clip(0.5f) == doctest::Approx(0.5f));
  CHECK(neon::soft_clip(-0.5f) == doctest::Approx(-0.5f));
  float prev = -1.0f;
  for (float x = -8.0f; x <= 8.0f; x += 0.01f) {
    const float y = neon::soft_clip(x);
    CHECK(y >= prev - 1e-6f);  // never folds back — that would be wrap
    CHECK(std::fabs(y) < 1.0f);
    prev = y;
  }
  CHECK(neon::soft_clip(1000.0f) < 1.0f);
  CHECK(neon::soft_clip(1000.0f) > 0.95f);
}

TEST_CASE("mixer: a full-scale Link tap is not crushed") {
  const auto hot = constant(0.99f);
  neon::MixSources src;
  src.link_in_l = hot.data();
  src.link_in_r = hot.data();
  neon::MixerConfig cfg;
  cfg.role_l = neon::AudioRole::kLinkIn;
  cfg.role_r = neon::AudioRole::kLinkIn;
  cfg.sub_gain = neon::kUnityGainByte;
  std::vector<float> l(kFrames), r(kFrames);
  neon::mix_block(cfg, src, kFrames, l.data(), r.data());
  CHECK(l[0] == doctest::Approx(0.99f));
  CHECK(r[0] == doctest::Approx(0.99f));
}

TEST_CASE("mixer: a hot mix saturates rather than wrapping") {
  const auto hot = constant(0.9f);
  neon::MixSources src;
  src.metro = hot.data();
  src.amy = hot.data();
  neon::MixerConfig cfg;
  cfg.metro_enabled = true;
  cfg.amy_enabled = true;
  std::vector<float> l(kFrames), r(kFrames);
  neon::mix_block(cfg, src, kFrames, l.data(), r.data());
  CHECK(l[0] > 0.9f);
  CHECK(l[0] < 1.0f);

  std::vector<int16_t> out(kFrames * 2);
  neon::float_to_int16(l.data(), r.data(), kFrames, out.data());
  CHECK(out[0] > 30000);
  CHECK(out[0] <= 32767);
}

TEST_CASE("float_to_int16: clamps beyond full scale in both directions") {
  std::vector<float> l{2.0f, -2.0f, 0.0f, 1.0f};
  std::vector<float> r{-5.0f, 5.0f, 0.0f, -1.0f};
  std::vector<int16_t> out(8);
  neon::float_to_int16(l.data(), r.data(), 4, out.data());
  CHECK(out[0] == 32767);
  CHECK(out[1] == -32768);
  CHECK(out[2] == -32768);
  CHECK(out[3] == 32767);
  CHECK(out[4] == 0);
  CHECK(out[5] == 0);
  CHECK(out[6] == 32767);
  CHECK(out[7] == -32767);  // symmetric scaling: -1.0 is -32767
}

TEST_CASE("stereo_to_mono_i16: averages without overflowing") {
  const int16_t in[6] = {32767, 32767, -32768, -32768, 100, -100};
  int16_t out[3] = {};
  neon::stereo_to_mono_i16(in, 3, out);
  CHECK(out[0] == 32767);
  CHECK(out[1] == -32768);
  CHECK(out[2] == 0);
}

TEST_CASE("int16_to_float: mono fans out to both sides") {
  const int16_t mono[3] = {16384, -16384, 0};
  float l[3] = {}, r[3] = {};
  neon::int16_to_float(mono, 1, 3, l, r);
  CHECK(l[0] == doctest::Approx(0.5f));
  CHECK(r[0] == doctest::Approx(0.5f));
  CHECK(l[1] == doctest::Approx(-0.5f));
  CHECK(r[2] == 0.0f);

  const int16_t stereo[4] = {16384, -16384, 0, 32767};
  neon::int16_to_float(stereo, 2, 2, l, r);
  CHECK(l[0] == doctest::Approx(0.5f));
  CHECK(r[0] == doctest::Approx(-0.5f));
  CHECK(l[1] == 0.0f);
}

TEST_CASE("peak_meter: instant attack, decaying release") {
  const std::vector<float> loud(32, 1.0f);
  const std::vector<float> quiet(32, 0.0f);
  const uint16_t p = neon::peak_meter(loud.data(), 32, 0);
  CHECK(p == 1000);
  uint16_t decayed = p;
  for (int i = 0; i < 5; ++i) {
    decayed = neon::peak_meter(quiet.data(), 32, decayed);
  }
  CHECK(decayed < p);
  CHECK(decayed > 0);
  for (int i = 0; i < 500; ++i) {
    decayed = neon::peak_meter(quiet.data(), 32, decayed);
  }
  CHECK(decayed == 0);
}
