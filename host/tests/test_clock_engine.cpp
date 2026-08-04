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

namespace {

neon::TimelineSnapshot snapshot_at(uint32_t milli_bpm, double beat_at_origin,
                                   int64_t origin_us, bool playing = true) {
  neon::TimelineSnapshot tl;
  tl.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(milli_bpm);
  tl.origin_us = origin_us;
  tl.beat_at_origin_q32 =
      static_cast<int64_t>(beat_at_origin * 4294967296.0);
  tl.playing = playing ? 1 : 0;
  return tl;
}

}  // namespace

TEST_CASE("retime: ticks land on session beats") {
  // Session at beat 0 exactly at origin: ticks every 125 ms from origin.
  neon::ClockEngine eng;
  neon::OutputSettings s;
  s.ppqn = 4;
  s.trig_len_us = 5000;
  eng.set_output(s);

  const int64_t origin = 1000000;
  eng.retime(snapshot_at(120000, 0.0, origin), origin);
  const auto edges = collect(eng, origin, origin + 1000000);
  REQUIRE(edges.size() == 16);
  for (int i = 0; i < 8; ++i) {
    CHECK(edges[2 * i].t_us == origin + i * 125000);
    CHECK(edges[2 * i].high);
  }
}

TEST_CASE("retime: anchors mid-beat to the next grid tick") {
  // Beat 0.1 at origin, 4 PPQN: next tick is beat 0.25, i.e. 75 ms later.
  neon::ClockEngine eng;
  neon::OutputSettings s;
  s.ppqn = 4;
  s.trig_len_us = 1000;
  eng.set_output(s);

  const int64_t origin = 500000;
  eng.retime(snapshot_at(120000, 0.1, origin), origin);
  const auto edges = collect(eng, origin, origin + 500000);
  REQUIRE(!edges.empty());
  CHECK(edges[0].t_us == origin + 75000);
  CHECK(edges[0].high);
  // Subsequent ticks stay on the 125 ms grid.
  REQUIRE(edges.size() >= 4);
  CHECK(edges[2].t_us == origin + 75000 + 125000);
}

TEST_CASE("retime: exact on-tick anchor emits at the anchor") {
  // Beat 2.5 at 4 PPQN is tick 10 exactly: first rise at origin itself.
  neon::ClockEngine eng;
  neon::OutputSettings s;
  s.ppqn = 4;
  s.trig_len_us = 1000;
  eng.set_output(s);

  const int64_t origin = 250000;
  eng.retime(snapshot_at(120000, 2.5, origin), origin);
  const auto edges = collect(eng, origin, origin + 200000);
  REQUIRE(!edges.empty());
  CHECK(edges[0].t_us == origin);
}

TEST_CASE("retime: negative session beats anchor correctly") {
  // Link beats can be negative before the session origin. Beat -1.75 at
  // 4 PPQN is tick -7 exactly: first rise at the origin itself.
  neon::ClockEngine eng;
  neon::OutputSettings s;
  s.ppqn = 4;
  s.trig_len_us = 1000;
  eng.set_output(s);

  const int64_t origin = 3000000;
  eng.retime(snapshot_at(120000, -1.75, origin), origin);
  const auto edges = collect(eng, origin, origin + 400000);
  REQUIRE(!edges.empty());
  CHECK(edges[0].t_us == origin);
  CHECK(edges[2].t_us == origin + 125000);
}

TEST_CASE("retime: tempo change re-anchors without reordering edges") {
  neon::ClockEngine eng;
  neon::OutputSettings s;
  s.ppqn = 4;
  s.trig_len_us = 2000;
  eng.set_output(s);

  eng.retime(snapshot_at(120000, 0.0, 0), 0);
  auto first = collect(eng, 0, 1000000);
  REQUIRE(!first.empty());

  // 140 BPM from t=1s; the session says beat 2.0 lands exactly there.
  eng.retime(snapshot_at(140000, 2.0, 1000000), 1000000);
  const auto second = collect(eng, 1000000, 2000000);
  REQUIRE(!second.empty());

  // New grid: 140 BPM, 4 PPQN -> ~107142.86 µs period from t=1s.
  CHECK(second[0].t_us == 1000000);
  CHECK(second[0].high);
  const int64_t second_period = second[2].t_us - second[0].t_us;
  CHECK(second_period >= 107142);
  CHECK(second_period <= 107143);

  // Continuity: nothing out of order across the retime boundary.
  int64_t last = first.back().t_us;
  for (const auto& e : second) {
    CHECK(e.t_us >= last);
    last = e.t_us;
  }
}

TEST_CASE("retime with transport gating: stop drains the pulse, start resumes") {
  neon::ClockEngine eng;
  eng.set_transport_gating(true);
  neon::OutputSettings s;
  s.ppqn = 4;
  s.trig_len_us = 5000;
  eng.set_output(s);

  eng.retime(snapshot_at(120000, 0.0, 0, true), 0);
  neon::Edge buf[8];
  // Consume the first rise so a fall is pending.
  REQUIRE(eng.generate(0, 1000, buf, 8) == 1);
  CHECK(buf[0].high);

  // Transport stops: the pending fall must still drain, then silence.
  eng.retime(snapshot_at(120000, 0.0, 0, false), 1000);
  const size_t n = eng.generate(1000, 1000000, buf, 8);
  REQUIRE(n == 1);
  CHECK_FALSE(buf[0].high);
  CHECK(buf[0].t_us == 5000);
  CHECK(eng.generate(1000, 10000000, buf, 8) == 0);

  // Transport starts again: rises resume on the session grid.
  eng.retime(snapshot_at(120000, 8.0, 2000000, true), 2000000);
  REQUIRE(eng.generate(2000000, 2200000, buf, 8) >= 1);
  CHECK(buf[0].high);
  CHECK(buf[0].t_us == 2000000);
}

TEST_CASE("retime without gating ignores the playing flag") {
  neon::ClockEngine eng;  // gating off by default (milestone 2 behavior)
  neon::OutputSettings s;
  s.ppqn = 4;
  s.trig_len_us = 1000;
  eng.set_output(s);

  eng.retime(snapshot_at(120000, 0.0, 0, false), 0);
  neon::Edge buf[4];
  CHECK(eng.generate(0, 500000, buf, 4) > 0);
}

TEST_CASE("engine emits nothing before reset or with zero tempo") {
  neon::ClockEngine eng;
  neon::Edge buf[4];
  CHECK(eng.generate(0, 1000000, buf, 4) == 0);

  eng.set_tempo(0);
  eng.reset(0);
  CHECK(eng.generate(0, 1000000, buf, 4) == 0);
}
