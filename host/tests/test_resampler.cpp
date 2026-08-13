#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "neon/audio/resampler.hpp"

namespace {

// A sine at `freq` Hz sampled at `rate`, stereo (right side inverted so
// channel independence is visible).
std::vector<int16_t> sine(uint32_t rate, double freq, uint32_t frames) {
  std::vector<int16_t> out(frames * 2);
  for (uint32_t i = 0; i < frames; ++i) {
    const double v = std::sin(2.0 * M_PI * freq * i / rate) * 0.8;
    out[2 * i] = static_cast<int16_t>(v * 32767.0);
    out[2 * i + 1] = static_cast<int16_t>(-v * 32767.0);
  }
  return out;
}

// Frequency of a signal, from the count of positive-going zero crossings.
double measure_freq(const std::vector<float>& x, uint32_t rate) {
  uint32_t crossings = 0;
  uint32_t first = 0;
  uint32_t last = 0;
  for (size_t i = 1; i < x.size(); ++i) {
    if (x[i - 1] <= 0.0f && x[i] > 0.0f) {
      if (crossings == 0) {
        first = static_cast<uint32_t>(i);
      }
      last = static_cast<uint32_t>(i);
      ++crossings;
    }
  }
  if (crossings < 2) {
    return 0.0;
  }
  return static_cast<double>(crossings - 1) * rate / (last - first);
}

}  // namespace

TEST_CASE("LinearResampler: ratio 1.0 is bit-exact") {
  neon::LinearResampler rs;
  rs.set_rates(44100, 44100);
  CHECK(rs.step_q32() == (1ull << 32));

  const auto in = sine(44100, 997.0, 512);
  std::vector<float> l(512), r(512);
  uint32_t consumed = 0;
  const uint32_t n = rs.process(in.data(), 512, &consumed, l.data(), r.data(),
                                512);
  CHECK(n == 512);
  CHECK(consumed == 512);
  for (uint32_t i = 0; i < 512; ++i) {
    CHECK(l[i] == static_cast<float>(in[2 * i]) / 32768.0f);
    CHECK(r[i] == static_cast<float>(in[2 * i + 1]) / 32768.0f);
  }
}

TEST_CASE("LinearResampler: 48k to 44.1k keeps the frequency") {
  neon::LinearResampler rs;
  rs.set_rates(48000, 44100);

  const uint32_t in_frames = 48000;  // one second
  const auto in = sine(48000, 1000.0, in_frames);
  std::vector<float> l, r;
  l.reserve(45000);
  r.reserve(45000);

  uint32_t offset = 0;
  std::vector<float> lbuf(256), rbuf(256);
  while (offset + 2 < in_frames) {
    uint32_t consumed = 0;
    const uint32_t n =
        rs.process(in.data() + offset * 2, in_frames - offset, &consumed,
                   lbuf.data(), rbuf.data(), 256);
    if (n == 0 && consumed == 0) {
      break;
    }
    l.insert(l.end(), lbuf.begin(), lbuf.begin() + n);
    r.insert(r.end(), rbuf.begin(), rbuf.begin() + n);
    offset += consumed;
  }

  // 48000 in at 44100 out is 44100 frames of output, ±1.
  CHECK(l.size() > 44000);
  CHECK(l.size() < 44200);
  CHECK(measure_freq(l, 44100) == doctest::Approx(1000.0).epsilon(0.005));
  // The right channel is the inverse of the left, and stays that way.
  for (size_t i = 0; i < l.size(); ++i) {
    CHECK(r[i] == doctest::Approx(-l[i]).epsilon(0.001));
  }
}

TEST_CASE("LinearResampler: 44.1k to 48k keeps the frequency") {
  neon::LinearResampler rs;
  rs.set_rates(44100, 48000);
  const uint32_t in_frames = 44100;
  const auto in = sine(44100, 440.0, in_frames);
  std::vector<float> out;
  uint32_t offset = 0;
  std::vector<float> buf(256);
  while (offset + 2 < in_frames) {
    uint32_t consumed = 0;
    const uint32_t n = rs.process(in.data() + offset * 2, in_frames - offset,
                                  &consumed, buf.data(), nullptr, 256);
    if (n == 0 && consumed == 0) break;
    out.insert(out.end(), buf.begin(), buf.begin() + n);
    offset += consumed;
  }
  CHECK(out.size() > 47000);
  CHECK(measure_freq(out, 48000) == doctest::Approx(440.0).epsilon(0.01));
}

TEST_CASE("LinearResampler: the trim is clamped and moves the step") {
  neon::LinearResampler rs;
  rs.set_rates(48000, 44100);
  const uint64_t base = rs.step_q32();

  rs.set_trim_ppm(500);
  CHECK(rs.trim_ppm() == 500);
  CHECK(rs.step_q32() > base);
  rs.set_trim_ppm(-500);
  CHECK(rs.step_q32() < base);

  rs.set_trim_ppm(100000);
  CHECK(rs.trim_ppm() == neon::LinearResampler::kMaxTrimPpm);
  rs.set_trim_ppm(-100000);
  CHECK(rs.trim_ppm() == -neon::LinearResampler::kMaxTrimPpm);

  // 500 ppm on a 1.088 step is about 544e-6 — visible, but only just.
  rs.set_trim_ppm(0);
  CHECK(rs.step_q32() == base);
}

TEST_CASE("LinearResampler: a starved call produces what it can") {
  neon::LinearResampler rs;
  rs.set_rates(48000, 44100);
  const auto in = sine(48000, 1000.0, 4);
  std::vector<float> l(64), r(64);
  uint32_t consumed = 0;
  const uint32_t n = rs.process(in.data(), 4, &consumed, l.data(), r.data(), 64);
  CHECK(n <= 4);
  CHECK(n >= 3);
  CHECK(consumed <= 4);

  // Nothing at all in: nothing out, nothing consumed.
  CHECK(rs.process(nullptr, 0, &consumed, l.data(), r.data(), 64) == 0);
  CHECK(consumed == 0);
}
