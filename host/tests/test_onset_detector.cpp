#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include "neon/audio/onset_detector.hpp"
#include "neon/audio/sample_clock.hpp"

namespace {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kBlock = 256;
constexpr float kPi = 3.14159265f;

uint64_t us_q32(uint32_t rate) {
  neon::SampleClock clk;
  clk.reset(rate);
  return clk.us_per_frame_q32();
}

int64_t stamp(int64_t t0_us, uint32_t i, uint64_t q32) {
  return t0_us + static_cast<int64_t>((static_cast<uint64_t>(i) * q32) >> 32);
}

void place_kick(std::vector<float>& L, std::vector<float>& R, uint32_t at,
                uint32_t rate, float peak, bool right_only = false) {
  // 2-cycle 60 Hz cosine * exp decay, peak at the first sample (the attack).
  const uint32_t n = static_cast<uint32_t>(rate / 30u);
  const float rf = static_cast<float>(rate);
  for (uint32_t i = 0; i < n && at + i < L.size(); ++i) {
    const float t = static_cast<float>(i) / rf;
    const float s =
        peak * std::exp(-t / 0.012f) * std::cos(2.f * kPi * 60.f * t);
    if (right_only) {
      L[at + i] = 0.f;
      R[at + i] = s;
    } else {
      L[at + i] = s;
      R[at + i] = s;
    }
  }
}

std::vector<neon::OnsetEvent> run_blocks(neon::OnsetDetector& d,
                                         const std::vector<float>& L,
                                         const std::vector<float>& R,
                                         uint32_t rate, int64_t t0_us) {
  const uint64_t q32 = us_q32(rate);
  std::vector<neon::OnsetEvent> hits;
  const uint32_t n = static_cast<uint32_t>(L.size());
  uint32_t off = 0;
  uint32_t block = 0;
  while (off < n) {
    const uint32_t take = n - off < kBlock ? n - off : kBlock;
    const int64_t t0 = stamp(t0_us, block * kBlock, q32);
    neon::OnsetEvent tmp[8];
    const uint32_t k =
        d.process(L.data() + off, R.data() + off, take, t0, q32, tmp, 8);
    for (uint32_t i = 0; i < k; ++i) {
      hits.push_back(tmp[i]);
    }
    off += take;
    ++block;
  }
  return hits;
}

}  // namespace

TEST_CASE("OnsetDetector: 48 kHz kick timestamp within 1 sample") {
  neon::OnsetDetector d;
  d.reset(kRate);
  const uint64_t q32 = us_q32(kRate);
  const uint32_t at = 64;
  std::vector<float> L(kBlock * 10, 0.f), R(kBlock * 10, 0.f);
  place_kick(L, R, at, kRate, 0.9f);
  const int64_t t0 = 1'000'000;
  const auto hits = run_blocks(d, L, R, kRate, t0);
  REQUIRE(hits.size() >= 1);
  const int64_t expect = stamp(t0, at, q32);
  const int64_t sample_us =
      stamp(t0, 1, q32) - stamp(t0, 0, q32);
  CHECK(std::llabs(hits[0].t_us - expect) <= sample_us);
}

TEST_CASE("OnsetDetector: stereo kick on R still detected") {
  neon::OnsetDetector d;
  d.reset(kRate);
  std::vector<float> L(kBlock * 8, 0.f), R(kBlock * 8, 0.f);
  place_kick(L, R, 32, kRate, 0.9f, /*right_only=*/true);
  const auto hits = run_blocks(d, L, R, kRate, 0);
  CHECK(hits.size() >= 1);
}

TEST_CASE("OnsetDetector: 10 ms pair collapses to one onset") {
  neon::OnsetDetector d;
  d.reset(kRate);
  std::vector<float> L(kBlock * 16, 0.f), R(kBlock * 16, 0.f);
  place_kick(L, R, 16, kRate, 0.9f);
  place_kick(L, R, 16 + kRate / 100, kRate, 0.9f);  // +10 ms
  const auto hits = run_blocks(d, L, R, kRate, 0);
  CHECK(hits.size() == 1);
}

TEST_CASE("OnsetDetector: adaptive thresh, noise only is silent") {
  neon::OnsetDetector d;
  d.reset(kRate);
  const uint32_t n = kRate * 2;  // 2 s
  std::vector<float> L(n, 0.f), R(n, 0.f);
  // Fade in a −20 dB 200 Hz floor so the startup jump is not an onset,
  // then hold. Kicks at −6 dB sit on that floor.
  const float rf = static_cast<float>(kRate);
  const uint32_t fade = kRate / 5;  // 200 ms
  for (uint32_t i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / rf;
    const float g = i < fade ? static_cast<float>(i) / static_cast<float>(fade)
                             : 1.f;
    const float floor = g * 0.1f * std::sin(2.f * kPi * 200.f * t);
    L[i] = floor;
    R[i] = floor;
  }
  const int64_t t_stable = 400000;  // well after the fade
  neon::OnsetDetector noise_only;
  noise_only.reset(kRate);
  const auto silent = run_blocks(noise_only, L, R, kRate, 0);
  uint32_t late_noise = 0;
  for (const auto& e : silent) {
    if (e.t_us >= t_stable) {
      ++late_noise;
    }
  }
  CHECK(late_noise == 0);

  place_kick(L, R, kRate, kRate, 0.5f);  // 1 s
  place_kick(L, R, kRate + kRate / 2, kRate, 0.5f);
  const auto hits = run_blocks(d, L, R, kRate, 0);
  uint32_t kicks = 0;
  for (const auto& e : hits) {
    if (e.t_us >= t_stable) {
      ++kicks;
    }
  }
  CHECK(kicks >= 2);
}

TEST_CASE("OnsetDetector: click-guard drops ±8 ms, keeps 20 ms") {
  const uint64_t q32 = us_q32(kRate);
  const int64_t t0 = 2'000'000;
  const uint32_t click_i = 80;

  {
    neon::OnsetDetector d;
    d.reset(kRate);
    d.note_click(stamp(t0, click_i, q32));
    std::vector<float> L(kBlock, 0.f), R(kBlock, 0.f);
    place_kick(L, R, click_i + 40, kRate, 0.9f);  // ~0.8 ms later
    neon::OnsetEvent tmp[4];
    const uint32_t k =
        d.process(L.data(), R.data(), kBlock, t0, q32, tmp, 4);
    CHECK(k == 0);
  }
  {
    neon::OnsetDetector d;
    d.reset(kRate);
    d.note_click(stamp(t0, click_i, q32));
    std::vector<float> L(kBlock * 8, 0.f), R(kBlock * 8, 0.f);
    const uint32_t later = click_i + (kRate * 20) / 1000;  // 20 ms
    place_kick(L, R, later, kRate, 0.9f);
    const auto hits = run_blocks(d, L, R, kRate, t0);
    CHECK(hits.size() >= 1);
  }
}

TEST_CASE("OnsetDetector: without note_click, in-block kick is kept") {
  // Stale ClickSynth::last_onset_frame must not plant a guard. The
  // detector only guards stamps that were note_click()'d.
  neon::OnsetDetector d;
  d.reset(kRate);
  const uint64_t q32 = us_q32(kRate);
  std::vector<float> L(kBlock, 0.f), R(kBlock, 0.f);
  place_kick(L, R, 173, kRate, 0.9f);
  neon::OnsetEvent tmp[4];
  const uint32_t k =
      d.process(L.data(), R.data(), kBlock, 0, q32, tmp, 4);
  CHECK(k >= 1);
}

TEST_CASE("OnsetDetector: click on last sample of N guards 2 ms into N+1") {
  neon::OnsetDetector d;
  d.reset(kRate);
  const uint64_t q32 = us_q32(kRate);
  const int64_t t0 = 5'000'000;
  d.note_click(stamp(t0, kBlock - 1, q32));

  std::vector<float> zL(kBlock, 0.f), zR(kBlock, 0.f);
  neon::OnsetEvent tmp[4];
  (void)d.process(zL.data(), zR.data(), kBlock, t0, q32, tmp, 4);

  const int64_t t1 = stamp(t0, kBlock, q32);
  const uint32_t i_2ms = (kRate * 2) / 1000;
  std::vector<float> L(kBlock, 0.f), R(kBlock, 0.f);
  place_kick(L, R, i_2ms, kRate, 0.9f);
  const uint32_t k =
      d.process(L.data(), R.data(), kBlock, t1, q32, tmp, 4);
  CHECK(k == 0);
}

TEST_CASE("OnsetDetector: sensitivity polarity (higher = easier)") {
  // slow/floor held at 0.02, attack flux ≈ 0.03 (peak 0.05 − floor 0.02).
  // sensitivity 255: scale = 0.35, thresh*1.8 = 0.02*0.35*1.8 = 0.0126 → detect
  // sensitivity 0:   scale = 2.00, thresh*1.8 = 0.02*2.00*1.8 = 0.072  → reject
  const uint64_t q32 = us_q32(kRate);
  const uint32_t settle = kRate;  // ~5 slow taus
  std::vector<float> L(settle + kBlock, 0.02f), R(settle + kBlock, 0.02f);
  L[settle] = 0.05f;
  R[settle] = 0.05f;

  auto run = [&](uint8_t sens) {
    neon::OnsetDetector d;
    d.reset(kRate);
    d.set_sensitivity(sens);
    neon::OnsetEvent tmp[4];
    uint32_t hits = 0;
    uint32_t off = 0;
    uint32_t block = 0;
    const int64_t t_peak = stamp(0, settle, q32);
    while (off < L.size()) {
      const uint32_t take =
          static_cast<uint32_t>(L.size()) - off < kBlock
              ? static_cast<uint32_t>(L.size()) - off
              : kBlock;
      const int64_t t0 = stamp(0, block * kBlock, q32);
      const uint32_t k =
          d.process(L.data() + off, R.data() + off, take, t0, q32, tmp, 4);
      for (uint32_t i = 0; i < k; ++i) {
        if (std::llabs(tmp[i].t_us - t_peak) <= 1000) {
          ++hits;
        }
      }
      off += take;
      ++block;
    }
    return hits;
  };

  CHECK(run(255) >= 1);
  CHECK(run(0) == 0);
}

TEST_CASE("OnsetDetector: reset(44100) vs reset(48000) wall-clock tau") {
  auto detect_at = [](uint32_t rate) {
    neon::OnsetDetector d;
    d.reset(rate);
    const uint64_t q32 = us_q32(rate);
    const uint32_t n = rate / 10;  // 100 ms
    std::vector<float> L(n, 0.f), R(n, 0.f);
    const uint32_t at = rate / 100;  // 10 ms in
    const float rf = static_cast<float>(rate);
    const uint32_t kick_n = rate / 30;
    for (uint32_t i = 0; i < kick_n && at + i < n; ++i) {
      const float t = static_cast<float>(i) / rf;
      const float s =
          0.9f * std::exp(-t / 0.012f) * std::cos(2.f * kPi * 60.f * t);
      L[at + i] = s;
      R[at + i] = s;
    }
    neon::OnsetEvent tmp[4];
    uint32_t hits = 0;
    int64_t t_hit = 0;
    const uint32_t block = 128;
    for (uint32_t off = 0, b = 0; off < n; off += block, ++b) {
      const uint32_t take = n - off < block ? n - off : block;
      const int64_t t0 = stamp(0, b * block, q32);
      const uint32_t k =
          d.process(L.data() + off, R.data() + off, take, t0, q32, tmp, 4);
      if (k > 0 && hits == 0) {
        t_hit = tmp[0].t_us;
      }
      hits += k;
    }
    const int64_t expect = stamp(0, at, q32);
    return std::pair<uint32_t, int64_t>{hits, t_hit - expect};
  };

  const auto a = detect_at(44100);
  const auto b = detect_at(48000);
  CHECK(a.first >= 1);
  CHECK(b.first >= 1);
  // Attack timestamp within 1 ms at either rate (taus are wall-clock).
  CHECK(std::llabs(a.second) <= 1000);
  CHECK(std::llabs(b.second) <= 1000);
}

TEST_CASE("OnsetDetector: block-phase timestamps are monotonic") {
  const uint64_t q32 = us_q32(kRate);
  const int64_t t0 = 9'000'000;

  neon::OnsetDetector a;
  a.reset(kRate);
  std::vector<float> L(kBlock, 0.f), R(kBlock, 0.f);
  place_kick(L, R, kBlock - 1, kRate, 0.9f);
  neon::OnsetEvent tmp[4];
  REQUIRE(a.process(L.data(), R.data(), kBlock, t0, q32, tmp, 4) >= 1);
  const int64_t t_last = tmp[0].t_us;

  neon::OnsetDetector b;
  b.reset(kRate);
  std::vector<float> L2(kBlock, 0.f), R2(kBlock, 0.f);
  place_kick(L2, R2, 0, kRate, 0.9f);
  const int64_t t1 = stamp(t0, kBlock, q32);
  REQUIRE(b.process(L2.data(), R2.data(), kBlock, t1, q32, tmp, 4) >= 1);
  CHECK(tmp[0].t_us > t_last);
}

TEST_CASE("OnsetDetector: CPU smoke 1000 blocks, no NaN") {
  neon::OnsetDetector d;
  d.reset(kRate);
  const uint64_t q32 = us_q32(kRate);
  std::vector<float> L(kBlock), R(kBlock);
  neon::OnsetEvent tmp[8];
  uint32_t seed = 1;
  for (int b = 0; b < 1000; ++b) {
    for (uint32_t i = 0; i < kBlock; ++i) {
      seed = seed * 1664525u + 1013904223u;
      const float n =
          (static_cast<float>(seed >> 8) / 16777216.f - 0.5f) * 0.05f;
      L[i] = n;
      R[i] = n;
    }
    if (b % 40 == 0) {
      place_kick(L, R, 8, kRate, 0.8f);
    }
    const int64_t t0 = stamp(0, static_cast<uint32_t>(b) * kBlock, q32);
    const uint32_t k =
        d.process(L.data(), R.data(), kBlock, t0, q32, tmp, 8);
    for (uint32_t i = 0; i < k; ++i) {
      CHECK_FALSE(std::isnan(tmp[i].strength));
      CHECK(tmp[i].strength > 0.f);
      CHECK(tmp[i].strength < 4.f);
    }
  }
}
