#include <doctest.h>

#include <cstdint>
#include <vector>

#include "neon/fixed_math.hpp"
#include "neon/pulse_channel.hpp"

namespace {

neon::TimelineSnapshot snapshot_at(uint32_t milli_bpm, double beat_at_origin,
                                   int64_t origin_us, bool playing = true) {
  neon::TimelineSnapshot tl;
  tl.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(milli_bpm);
  tl.origin_us = origin_us;
  tl.beat_at_origin_q32 = static_cast<int64_t>(beat_at_origin * 4294967296.0);
  tl.playing = playing ? 1 : 0;
  return tl;
}

std::vector<neon::Edge> drain(neon::PulseChannel& ch, int64_t t1) {
  std::vector<neon::Edge> all;
  neon::Edge e;
  while (ch.peek(&e) && e.t_us < t1) {
    all.push_back(e);
    ch.pop();
  }
  return all;
}

neon::PulseChannel make_channel(const neon::ClockOutputConfig& cfg,
                                uint32_t milli_bpm, double beat0,
                                int64_t origin) {
  neon::PulseChannel ch;
  ch.configure(cfg);
  ch.retime(snapshot_at(milli_bpm, beat0, origin), origin, true);
  return ch;
}

neon::ClockOutputConfig basic(uint32_t ppqn, uint32_t trig_len = 5000) {
  neon::ClockOutputConfig c;
  c.ppqn = ppqn;
  c.trig_len_us = trig_len;
  return c;
}

}  // namespace

TEST_CASE("tempo conversion: exact for 120 BPM") {
  CHECK(neon::micros_per_beat_q32_from_milli_bpm(120000) == (500000ull << 32));
}

TEST_CASE("120 BPM, 4 PPQN: exact 125 ms grid with 5 ms triggers") {
  auto ch = make_channel(basic(4), 120000, 0.0, 0);
  const auto edges = drain(ch, 1000000);
  REQUIRE(edges.size() == 16);
  for (int i = 0; i < 8; ++i) {
    CHECK(edges[2 * i].high);
    CHECK(edges[2 * i].t_us == i * 125000);
    CHECK_FALSE(edges[2 * i + 1].high);
    CHECK(edges[2 * i + 1].t_us == edges[2 * i].t_us + 5000);
  }
}

TEST_CASE("beat boundaries are drift-free for plain PPQN rates") {
  for (uint32_t ppqn : {1u, 2u, 3u, 4u, 7u, 24u, 48u, 96u}) {
    CAPTURE(ppqn);
    auto ch = make_channel(basic(ppqn, 1000), 120000, 0.0, 0);
    const auto edges = drain(ch, 100 * 500000);
    for (uint32_t beat = 0; beat < 100; ++beat) {
      const size_t rise_idx = static_cast<size_t>(2) * beat * ppqn;
      REQUIRE(rise_idx < edges.size());
      CHECK(edges[rise_idx].t_us == static_cast<int64_t>(beat) * 500000);
    }
  }
}

TEST_CASE("rational rates: mult and div make exact rational grids") {
  // 4 PPQN with div=3 -> 4 pulses per 3 beats: tick 4 lands exactly at
  // beat 3 (1.5 s at 120 BPM); tick 8 at beat 6.
  neon::ClockOutputConfig c = basic(4, 1000);
  c.div = 3;
  auto ch = make_channel(c, 120000, 0.0, 0);
  const auto edges = drain(ch, 6000000);
  REQUIRE(edges.size() >= 18);
  CHECK(edges[0].t_us == 0);
  CHECK(edges[2 * 4].t_us == 1500000);
  CHECK(edges[2 * 8].t_us == 3000000);

  // 1 PPQN with mult=3 -> 3 pulses per beat.
  neon::ClockOutputConfig m = basic(1, 1000);
  m.mult = 3;
  auto ch2 = make_channel(m, 120000, 0.0, 0);
  const auto e2 = drain(ch2, 1000001);
  REQUIRE(e2.size() >= 12);
  CHECK(e2[0].t_us == 0);
  CHECK(e2[2 * 3].t_us == 500000);  // tick 3 exactly at beat 1
  CHECK(e2[2 * 6].t_us == 1000000);
}

TEST_CASE("square mode: fall at duty percent of the period") {
  neon::ClockOutputConfig c = basic(4);
  c.mode = neon::ClockOutputConfig::PulseMode::kSquare;
  c.duty_pct = 25;
  auto ch = make_channel(c, 120000, 0.0, 0);
  const auto edges = drain(ch, 500000);
  REQUIRE(edges.size() >= 4);
  // Period 125000 -> high for 31250.
  CHECK(edges[0].t_us == 0);
  CHECK(edges[1].t_us == 31250);
  CHECK(edges[2].t_us == 125000);
  CHECK(edges[3].t_us == 125000 + 31250);
}

TEST_CASE("shuffle: odd pulses delayed, even pulses on the grid") {
  neon::ClockOutputConfig c = basic(4, 1000);
  c.shuffle_pct = 50;
  auto ch = make_channel(c, 120000, 0.0, 0);
  const auto edges = drain(ch, 1000000);
  REQUIRE(edges.size() >= 12);
  // Period 125000, swing 62500: ticks at 0, 187500, 250000, 437500, ...
  CHECK(edges[0].t_us == 0);
  CHECK(edges[2].t_us == 187500);
  CHECK(edges[4].t_us == 250000);
  CHECK(edges[6].t_us == 437500);
  // Monotonic despite the swing.
  int64_t last = INT64_MIN;
  for (const auto& e : edges) {
    CHECK(e.t_us >= last);
    last = e.t_us;
  }
}

TEST_CASE("shuffle parity is anchored to the absolute grid across retimes") {
  neon::ClockOutputConfig c = basic(4, 1000);
  c.shuffle_pct = 50;
  // Anchor mid-stream at beat 10.25 (tick 41, odd): the first pulse must
  // be swung.
  auto ch = make_channel(c, 120000, 10.25, 5000000);
  neon::Edge e;
  REQUIRE(ch.peek(&e));
  CHECK(e.t_us == 5000000 + 62500);
}

TEST_CASE("latency shifts every edge uniformly, including negative") {
  for (int32_t lat : {0, 3000, -3000}) {
    CAPTURE(lat);
    neon::PulseChannel ch;
    ch.configure(basic(4, 5000));
    ch.set_latency(lat);
    ch.retime(snapshot_at(120000, 0.0, 100000), 100000, true);
    const auto edges = drain(ch, 100000 + 500000 + lat);
    REQUIRE(edges.size() >= 8);
    for (int i = 0; i < 4; ++i) {
      CHECK(edges[2 * i].t_us == 100000 + i * 125000 + lat);
      CHECK(edges[2 * i + 1].t_us == 100000 + i * 125000 + 5000 + lat);
    }
  }
}

TEST_CASE("retime: anchors mid-beat to the next grid tick") {
  auto ch = make_channel(basic(4, 1000), 120000, 0.1, 500000);
  neon::Edge e;
  REQUIRE(ch.peek(&e));
  CHECK(e.t_us == 500000 + 75000);  // next tick is beat 0.25
}

TEST_CASE("retime: negative session beats anchor correctly") {
  auto ch = make_channel(basic(4, 1000), 120000, -1.75, 3000000);
  const auto edges = drain(ch, 3000000 + 400000);
  REQUIRE(!edges.empty());
  CHECK(edges[0].t_us == 3000000);  // beat -1.75 is tick -7 exactly
  CHECK(edges[2].t_us == 3000000 + 125000);
}

TEST_CASE("retime: tempo change does not reorder edges") {
  auto ch = make_channel(basic(4, 2000), 120000, 0.0, 0);
  auto first = drain(ch, 1000000);
  REQUIRE(!first.empty());
  ch.retime(snapshot_at(140000, 2.0, 1000000), 1000000, true);
  const auto second = drain(ch, 2000000);
  REQUIRE(!second.empty());
  CHECK(second[0].t_us == 1000000);
  const int64_t period = second[2].t_us - second[0].t_us;
  CHECK(period >= 107142);
  CHECK(period <= 107143);
  int64_t last = first.back().t_us;
  for (const auto& e : second) {
    CHECK(e.t_us >= last);
    last = e.t_us;
  }
}

TEST_CASE("stopping drains the in-flight pulse then goes silent") {
  auto ch = make_channel(basic(4, 5000), 120000, 0.0, 0);
  neon::Edge e;
  REQUIRE(ch.peek(&e));
  CHECK(e.high);
  ch.pop();  // rise consumed; fall pending
  ch.retime(snapshot_at(120000, 0.0, 0, false), 1000, false);
  REQUIRE(ch.peek(&e));
  CHECK_FALSE(e.high);
  CHECK(e.t_us == 5000);
  ch.pop();
  CHECK_FALSE(ch.peek(&e));
}

TEST_CASE("determinism: identical runs produce identical edges") {
  auto a = make_channel(basic(96, 1500), 98765, -0.5, -50000);
  auto b = make_channel(basic(96, 1500), 98765, -0.5, -50000);
  const auto ea = drain(a, 3000000);
  const auto eb = drain(b, 3000000);
  REQUIRE(ea.size() == eb.size());
  for (size_t i = 0; i < ea.size(); ++i) {
    CHECK(ea[i].t_us == eb[i].t_us);
    CHECK(ea[i].high == eb[i].high);
  }
}

TEST_CASE("trigger length clamps below one period") {
  auto ch = make_channel(basic(96, 10000), 120000, 0.0, 0);
  const auto edges = drain(ch, 500000);
  REQUIRE(edges.size() >= 6);
  for (size_t i = 0; i + 2 < edges.size(); i += 2) {
    CHECK(edges[i].high);
    CHECK_FALSE(edges[i + 1].high);
    CHECK(edges[i + 1].t_us > edges[i].t_us);
    CHECK(edges[i + 1].t_us < edges[i + 2].t_us);
  }
}

TEST_CASE("disabled or unretimed channel emits nothing") {
  neon::PulseChannel ch;
  neon::ClockOutputConfig c = basic(4);
  c.enabled = false;
  ch.configure(c);
  ch.retime(snapshot_at(120000, 0.0, 0), 0, true);
  neon::Edge e;
  CHECK_FALSE(ch.peek(&e));

  neon::PulseChannel idle;
  idle.configure(basic(4));
  CHECK_FALSE(idle.peek(&e));
}

TEST_CASE("anchoring far ahead of the snapshot lands on the same grid") {
  // The engine retimes at a cursor that runs ahead of the Link snapshot's
  // origin. Anchoring at from_us must give the same edges as a snapshot
  // captured at that instant — i.e. the beat conversion has to be exact,
  // not merely recovered by the post-anchor catch-up loop.
  neon::ClockOutputConfig cfg;
  cfg.ppqn = 4;

  neon::PulseChannel near;
  neon::PulseChannel far;
  near.configure(cfg);
  far.configure(cfg);

  neon::TimelineSnapshot at_origin;
  at_origin.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(137000);
  at_origin.origin_us = 4000000;
  at_origin.beat_at_origin_q32 = static_cast<int64_t>(9.125 * 4294967296.0);
  at_origin.quantum_beats = 4;
  at_origin.playing = 1;

  near.retime(at_origin, 4000000, true);
  far.retime(at_origin, 4000000, true);
  // Advance `far`'s anchor point 3 seconds past the snapshot origin.
  far.retime(at_origin, 7000000, true);

  neon::Edge e{};
  std::vector<int64_t> near_rises;
  for (int i = 0; i < 400 && near.peek(&e); ++i) {
    if (e.high && e.t_us >= 7000000) {
      near_rises.push_back(e.t_us);
    }
    near.pop();
    if (near_rises.size() >= 8) break;
  }
  std::vector<int64_t> far_rises;
  for (int i = 0; i < 400 && far.peek(&e); ++i) {
    if (e.high) {
      far_rises.push_back(e.t_us);
    }
    far.pop();
    if (far_rises.size() >= 8) break;
  }
  REQUIRE(near_rises.size() == 8);
  REQUIRE(far_rises.size() == 8);
  for (size_t i = 0; i < 8; ++i) {
    CHECK(far_rises[i] == near_rises[i]);
  }
}
