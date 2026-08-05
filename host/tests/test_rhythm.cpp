#include <doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "neon/fixed_math.hpp"
#include "neon/pulse_channel.hpp"
#include "neon/rhythm/euclid.hpp"
#include "neon/rhythm/random.hpp"

namespace {

std::string pattern(uint32_t steps, uint32_t fills, uint32_t rot = 0) {
  std::string s;
  for (uint32_t i = 0; i < steps; ++i) {
    s += neon::euclid_hit(i, steps, fills, rot) ? 'x' : '.';
  }
  return s;
}

neon::TimelineSnapshot snapshot_120(int64_t origin) {
  neon::TimelineSnapshot tl;
  tl.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(120000);
  tl.origin_us = origin;
  tl.beat_at_origin_q32 = 0;
  tl.playing = 1;
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

}  // namespace

TEST_CASE("euclid: canonical patterns (up to rotation)") {
  CHECK(pattern(8, 3) == "x..x..x.");
  // E(5,8): the Bresenham form is a rotation of Bjorklund's x.xx.xx. —
  // same cyclic gap sequence (2,1,2,1,2), hit at step 0.
  CHECK(pattern(8, 5) == "x.x.xx.x");
  CHECK(pattern(16, 4) == "x...x...x...x...");
  CHECK(pattern(4, 1) == "x...");
  CHECK(pattern(8, 8) == "xxxxxxxx");
  CHECK(pattern(8, 0) == "........");
}

TEST_CASE("euclid: maximal evenness (gap lengths differ by at most 1)") {
  for (uint32_t n : {8u, 13u, 16u}) {
    for (uint32_t k = 1; k < n; ++k) {
      CAPTURE(n);
      CAPTURE(k);
      std::vector<uint32_t> hits;
      for (uint32_t i = 0; i < n; ++i) {
        if (neon::euclid_hit(i, n, k, 0)) hits.push_back(i);
      }
      REQUIRE(hits.size() == k);
      uint32_t gmin = n;
      uint32_t gmax = 0;
      for (size_t i = 0; i < hits.size(); ++i) {
        const uint32_t next = i + 1 < hits.size() ? hits[i + 1] : hits[0] + n;
        const uint32_t gap = next - hits[i];
        gmin = gap < gmin ? gap : gmin;
        gmax = gap > gmax ? gap : gmax;
      }
      CHECK(gmax - gmin <= 1);
    }
  }
}

TEST_CASE("euclid: rotation shifts the pattern") {
  CHECK(pattern(8, 3, 3) == pattern(8, 3).substr(3) + pattern(8, 3).substr(0, 3));
  // Total fills unchanged under rotation.
  for (uint32_t rot = 0; rot < 8; ++rot) {
    int hits = 0;
    for (uint32_t i = 0; i < 8; ++i) {
      hits += neon::euclid_hit(i, 8, 3, rot) ? 1 : 0;
    }
    CHECK(hits == 3);
  }
}

TEST_CASE("probability: deterministic and proportional") {
  // Deterministic: same tick, same verdict.
  for (uint64_t t = 0; t < 100; ++t) {
    CHECK(neon::probability_hit(t, 40) == neon::probability_hit(t, 40));
  }
  CHECK(neon::probability_hit(7, 100));
  CHECK_FALSE(neon::probability_hit(7, 0));
  // Proportional over many ticks (loose bounds).
  int hits = 0;
  for (uint64_t t = 0; t < 10000; ++t) {
    hits += neon::probability_hit(t, 30) ? 1 : 0;
  }
  CHECK(hits > 2600);
  CHECK(hits < 3400);
}

TEST_CASE("euclid channel: only pattern steps fire, locked to the grid") {
  neon::ClockOutputConfig cfg;
  cfg.ppqn = 4;  // 16ths at 125 ms
  cfg.trig_len_us = 1000;
  cfg.rhythm = neon::ClockOutputConfig::RhythmMode::kEuclid;
  cfg.euclid_steps = 8;
  cfg.euclid_fills = 3;  // x..x..x.
  neon::PulseChannel ch;
  ch.configure(cfg);
  ch.retime(snapshot_120(0), 0, true);

  const auto edges = drain(ch, 2000000);  // one full 8-step cycle = 1 s
  std::vector<int64_t> rises;
  for (const auto& e : edges) {
    if (e.high) rises.push_back(e.t_us);
  }
  REQUIRE(rises.size() >= 6);
  // Pattern hits at ticks 0,3,6,8,11,14 -> times k*125000.
  CHECK(rises[0] == 0);
  CHECK(rises[1] == 3 * 125000);
  CHECK(rises[2] == 6 * 125000);
  CHECK(rises[3] == 8 * 125000);
  CHECK(rises[4] == 11 * 125000);
  CHECK(rises[5] == 14 * 125000);
}

TEST_CASE("euclid channel: pattern position survives a retime") {
  neon::ClockOutputConfig cfg;
  cfg.ppqn = 4;
  cfg.trig_len_us = 1000;
  cfg.rhythm = neon::ClockOutputConfig::RhythmMode::kEuclid;
  cfg.euclid_steps = 8;
  cfg.euclid_fills = 3;
  neon::PulseChannel ch;
  ch.configure(cfg);
  // Anchor mid-cycle: beat 1 = tick 4; next pattern hit is tick 6.
  ch.retime(snapshot_120(0), 0, true);
  neon::Edge e;
  // Fresh channel anchored at beat 1:
  neon::PulseChannel ch2;
  ch2.configure(cfg);
  neon::TimelineSnapshot tl = snapshot_120(0);
  tl.beat_at_origin_q32 = 1ll << 32;  // beat 1 at t=0
  ch2.retime(tl, 0, true);
  REQUIRE(ch2.peek(&e));
  CHECK(e.t_us == 2 * 125000);  // tick 6 is 2 ticks (250 ms) after beat 1
}

TEST_CASE("probability channel: silent config parks, live config plays") {
  neon::ClockOutputConfig cfg;
  cfg.ppqn = 4;
  cfg.rhythm = neon::ClockOutputConfig::RhythmMode::kProbability;
  cfg.probability_pct = 0;
  neon::PulseChannel ch;
  ch.configure(cfg);
  ch.retime(snapshot_120(0), 0, true);
  neon::Edge e;
  CHECK_FALSE(ch.peek(&e));

  cfg.probability_pct = 50;
  neon::PulseChannel ch2;
  ch2.configure(cfg);
  ch2.retime(snapshot_120(0), 0, true);
  const auto edges = drain(ch2, 4000000);  // 32 ticks
  size_t rises = 0;
  for (const auto& ed : edges) {
    if (ed.high) {
      ++rises;
      CHECK(ed.t_us % 125000 == 0);  // hits stay on the grid
    }
  }
  CHECK(rises > 6);
  CHECK(rises < 26);
}

TEST_CASE("humanize: bounded, deterministic, never reorders") {
  neon::ClockOutputConfig cfg;
  cfg.ppqn = 4;
  cfg.trig_len_us = 1000;
  cfg.humanize_pct = 40;  // up to 50 ms at 125 ms period
  neon::PulseChannel a;
  a.configure(cfg);
  a.retime(snapshot_120(0), 0, true);
  const auto ea = drain(a, 2000000);

  neon::PulseChannel b;
  b.configure(cfg);
  b.retime(snapshot_120(0), 0, true);
  const auto eb = drain(b, 2000000);

  REQUIRE(ea.size() == eb.size());
  int64_t last = INT64_MIN;
  bool any_offset = false;
  size_t rise_idx = 0;
  for (size_t i = 0; i < ea.size(); ++i) {
    CHECK(ea[i].t_us == eb[i].t_us);  // deterministic
    CHECK(ea[i].t_us >= last);        // ordered
    last = ea[i].t_us;
    if (ea[i].high) {
      const int64_t grid = static_cast<int64_t>(rise_idx) * 125000;
      CHECK(ea[i].t_us >= grid);
      CHECK(ea[i].t_us <= grid + 50000);
      if (ea[i].t_us != grid) any_offset = true;
      ++rise_idx;
    }
  }
  CHECK(any_offset);
}
