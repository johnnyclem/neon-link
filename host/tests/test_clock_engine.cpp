#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cstdint>
#include <vector>

#include "neon/clock_engine.hpp"

namespace {

std::vector<neon::Edge> collect(neon::ClockEngine& eng, int64_t t0, int64_t t1,
                                size_t chunk = 8) {
  std::vector<neon::Edge> all;
  std::vector<neon::Edge> buf(chunk);
  size_t n;
  do {
    n = eng.generate(t0, t1, buf.data(), buf.size());
    all.insert(all.end(), buf.begin(), buf.begin() + n);
  } while (n == chunk);
  return all;
}

neon::ClockEngine make_engine(uint32_t milli_bpm, uint32_t ppqn,
                              uint32_t trig_len_us, int64_t origin) {
  neon::ClockEngine eng;
  eng.set_tempo(neon::micros_per_beat_q32_from_milli_bpm(milli_bpm));
  neon::OutputSettings s;
  s.ppqn = ppqn;
  s.trig_len_us = trig_len_us;
  eng.set_output(s);
  eng.reset(origin);
  return eng;
}

}  // namespace

TEST_CASE("tempo conversion: exact for 120 BPM") {
  // 120 BPM -> 500000 µs/beat, representable exactly.
  CHECK(neon::micros_per_beat_q32_from_milli_bpm(120000) ==
        (500000ull << 32));
}

TEST_CASE("tempo conversion: clamps below 1 BPM without overflow") {
  const uint64_t at_floor = neon::micros_per_beat_q32_from_milli_bpm(1000);
  CHECK(neon::micros_per_beat_q32_from_milli_bpm(0) == at_floor);
  CHECK(neon::micros_per_beat_q32_from_milli_bpm(999) == at_floor);
  CHECK((at_floor >> 32) == 60000000ull);  // 60 s per beat at 1 BPM
}

TEST_CASE("120 BPM, 4 PPQN: exact 125 ms grid with 5 ms triggers") {
  auto eng = make_engine(120000, 4, 5000, 0);
  const auto edges = collect(eng, 0, 1000000);  // one second

  REQUIRE(edges.size() == 16);  // 8 rises + 8 falls
  for (int i = 0; i < 8; ++i) {
    const auto& rise = edges[2 * i];
    const auto& fall = edges[2 * i + 1];
    CHECK(rise.high);
    CHECK(rise.t_us == i * 125000);
    CHECK_FALSE(fall.high);
    CHECK(fall.t_us == rise.t_us + 5000);
  }
}

TEST_CASE("edges are non-decreasing in time and alternate rise/fall") {
  auto eng = make_engine(133333, 24, 2000, 12345);
  const auto edges = collect(eng, 12345, 12345 + 5000000);
  REQUIRE(!edges.empty());
  bool expect_high = true;
  int64_t last = INT64_MIN;
  for (const auto& e : edges) {
    CHECK(e.t_us >= last);
    CHECK(e.high == expect_high);
    last = e.t_us;
    expect_high = !expect_high;
  }
}

TEST_CASE("beat boundaries are drift-free: tick ppqn lands exactly on beat") {
  // For any ppqn, the rational accumulator must make ppqn ticks sum to
  // exactly one beat (500000 µs at 120 BPM) — no floor-division drift.
  for (uint32_t ppqn : {1u, 2u, 3u, 4u, 7u, 24u, 48u, 96u}) {
    CAPTURE(ppqn);
    auto eng = make_engine(120000, ppqn, 1000, 0);
    // 100 beats worth of edges.
    const auto edges = collect(eng, 0, 100 * 500000, 64);
    for (uint32_t beat = 0; beat < 100; ++beat) {
      const size_t rise_idx = static_cast<size_t>(2) * beat * ppqn;
      REQUIRE(rise_idx < edges.size());
      CHECK(edges[rise_idx].t_us == static_cast<int64_t>(beat) * 500000);
    }
  }
}

TEST_CASE("window continuity: split windows equal one big window") {
  auto whole = make_engine(174000, 24, 3000, 0);
  auto split = make_engine(174000, 24, 3000, 0);

  const auto big = collect(whole, 0, 2000000);

  std::vector<neon::Edge> pieces;
  for (int64_t t = 0; t < 2000000; t += 7001) {  // deliberately odd stride
    const int64_t t1 = t + 7001 < 2000000 ? t + 7001 : 2000000;
    const auto part = collect(split, t, t1);
    pieces.insert(pieces.end(), part.begin(), part.end());
  }

  REQUIRE(big.size() == pieces.size());
  for (size_t i = 0; i < big.size(); ++i) {
    CHECK(big[i].t_us == pieces[i].t_us);
    CHECK(big[i].high == pieces[i].high);
  }
}

TEST_CASE("determinism: identical runs produce identical edges") {
  auto a = make_engine(98765, 96, 1500, -50000);
  auto b = make_engine(98765, 96, 1500, -50000);
  const auto ea = collect(a, -50000, 3000000);
  const auto eb = collect(b, -50000, 3000000);
  REQUIRE(ea.size() == eb.size());
  for (size_t i = 0; i < ea.size(); ++i) {
    CHECK(ea[i].t_us == eb[i].t_us);
    CHECK(ea[i].high == eb[i].high);
  }
}

TEST_CASE("trigger length clamps below one period") {
  // 120 BPM, 96 PPQN -> period ≈ 5208 µs; a 10 ms trigger must be clamped
  // so every cycle still contains a low phase.
  auto eng = make_engine(120000, 96, 10000, 0);
  const auto edges = collect(eng, 0, 500000);
  REQUIRE(edges.size() >= 4);
  for (size_t i = 0; i + 2 < edges.size(); i += 2) {
    const auto& rise = edges[i];
    const auto& fall = edges[i + 1];
    const auto& next_rise = edges[i + 2];
    CHECK(rise.high);
    CHECK_FALSE(fall.high);
    CHECK(fall.t_us > rise.t_us);
    CHECK(fall.t_us < next_rise.t_us);
  }
}

TEST_CASE("small output buffers never lose or reorder edges") {
  auto ref = make_engine(120000, 24, 4000, 0);
  const auto expected = collect(ref, 0, 1000000, 1024);

  auto eng = make_engine(120000, 24, 4000, 0);
  const auto got = collect(eng, 0, 1000000, 1);  // worst case: 1-edge buffer

  REQUIRE(expected.size() == got.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    CHECK(expected[i].t_us == got[i].t_us);
    CHECK(expected[i].high == got[i].high);
  }
}

TEST_CASE("engine emits nothing before reset or with zero tempo") {
  neon::ClockEngine eng;
  neon::Edge buf[4];
  CHECK(eng.generate(0, 1000000, buf, 4) == 0);

  eng.set_tempo(0);
  eng.reset(0);
  CHECK(eng.generate(0, 1000000, buf, 4) == 0);
}
