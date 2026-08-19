#include <doctest.h>

#include <cstdint>
#include <vector>

#include "neon/fixed_math.hpp"
#include "neon/midi/clock_engine.hpp"
#include "neon/transport.hpp"

namespace {

neon::TimelineSnapshot snapshot(uint32_t milli_bpm, double beat_at_origin,
                                int64_t origin_us, uint8_t playing = 1,
                                uint32_t quantum = 4) {
  neon::TimelineSnapshot tl;
  tl.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(milli_bpm);
  tl.origin_us = origin_us;
  tl.beat_at_origin_q32 = static_cast<int64_t>(beat_at_origin * 4294967296.0);
  tl.quantum_beats = quantum;
  tl.playing = playing;
  return tl;
}

std::vector<neon::midi::Event> drain(neon::midi::ClockEngine& eng, int64_t from,
                                     int64_t until) {
  std::vector<neon::midi::Event> all;
  neon::midi::Event buf[16];
  int64_t cursor = from;
  for (int i = 0; i < 8192 && cursor < until; ++i) {
    const size_t n = eng.generate(cursor, until, buf, 16);
    for (size_t k = 0; k < n; ++k) {
      all.push_back(buf[k]);
    }
    if (n < 16) {
      break;
    }
    if (buf[n - 1].t_us <= cursor) {
      break;
    }
    cursor = buf[n - 1].t_us;
  }
  return all;
}

}  // namespace

TEST_CASE("SPP encoder is 0xF2 plus 14-bit little-endian 7-bit bytes") {
  uint8_t buf[3] = {};
  CHECK(neon::midi::song_position(256, buf) == 3);
  CHECK(buf[0] == neon::midi::kSongPosition);
  CHECK(buf[1] == 0x00);  // 256 = 0b10_00000000 → lsb 0, msb 2
  CHECK(buf[2] == 0x02);

  CHECK(neon::midi::song_position(0x3fff, buf) == 3);
  CHECK(buf[1] == 0x7f);
  CHECK(buf[2] == 0x7f);
}

TEST_CASE("song position is four sixteenths per beat, wrapped to 14 bits") {
  const auto tl = snapshot(120000, 64.0, 0);  // bar 17 in 4/4
  CHECK(neon::midi::song_position_16ths(tl, 0) == 256);
  const auto mid = snapshot(120000, 17.25, 0);
  CHECK(neon::midi::song_position_16ths(mid, 0) == 69);
}

TEST_CASE("24 PPQN clocks land on the beat grid with no long-run drift") {
  const auto tl = snapshot(120000, 0.0, 0);
  // 120 BPM → 500_000 µs/beat. 24 clocks in (0, 500000].
  CHECK(neon::midi::next_clock_us(tl, 0) == 500000 / 24);

  neon::midi::ClockEngine eng;
  eng.retime(tl, 0);
  const auto evs = drain(eng, 0, 500000);
  int clocks = 0;
  int64_t last_clock = 0;
  for (const auto& e : evs) {
    if (e.kind == neon::midi::EventKind::Clock) {
      ++clocks;
      last_clock = e.t_us;
    }
  }
  CHECK(clocks == 24);
  CHECK(last_clock == 500000);

  // A hundred beats later the grid is still exact (24 clocks/beat).
  neon::midi::ClockEngine long_run;
  long_run.retime(tl, 0);
  const auto long_evs = drain(long_run, 0, 500000ll * 100);
  int64_t last = 0;
  int nclock = 0;
  for (const auto& e : long_evs) {
    if (e.kind == neon::midi::EventKind::Clock) {
      last = e.t_us;
      ++nclock;
    }
  }
  CHECK(nclock == 24 * 100);
  CHECK(last == 500000ll * 100);
}

TEST_CASE("start at song position 0 emits Start then clocks, never Continue") {
  const auto tl = snapshot(120000, 0.0, 0, /*playing=*/1);
  neon::midi::ClockEngine eng;
  eng.retime(tl, 0);
  const auto evs = drain(eng, 0, 100000);
  REQUIRE(evs.size() >= 2);
  CHECK(evs[0].kind == neon::midi::EventKind::Start);
  CHECK(evs[0].t_us < evs[1].t_us);
  bool saw_clock = false;
  for (const auto& e : evs) {
    CHECK(e.kind != neon::midi::EventKind::Cont);
    CHECK(e.kind != neon::midi::EventKind::Spp);
    if (e.kind == neon::midi::EventKind::Clock) {
      saw_clock = true;
    }
  }
  CHECK(saw_clock);
}

TEST_CASE("joining bar 17 emits SPP 256 then Continue, not Start") {
  // Bar 17, 4/4 → beat 64 → 256 sixteenths.
  const auto tl = snapshot(120000, 64.0, 0, /*playing=*/1);
  neon::midi::ClockEngine eng;
  eng.retime(tl, 0);
  const auto evs = drain(eng, 0, 100000);
  REQUIRE(evs.size() >= 3);
  CHECK(evs[0].kind == neon::midi::EventKind::Spp);
  CHECK(evs[0].spp == 256);
  CHECK(evs[1].kind == neon::midi::EventKind::Cont);
  CHECK(evs[1].t_us >= evs[0].t_us);
  bool saw_start = false;
  bool saw_clock = false;
  for (const auto& e : evs) {
    if (e.kind == neon::midi::EventKind::Start) {
      saw_start = true;
    }
    if (e.kind == neon::midi::EventKind::Clock) {
      saw_clock = true;
      CHECK(e.t_us >= evs[1].t_us);
    }
  }
  CHECK_FALSE(saw_start);
  CHECK(saw_clock);

  uint8_t buf[3] = {};
  CHECK(neon::midi::encode_event(evs[0], buf) == 3);
  CHECK(buf[0] == 0xf2);
}

TEST_CASE("stop emits a single Stop and then silence") {
  auto tl = snapshot(120000, 0.0, 0, /*playing=*/1);
  neon::midi::ClockEngine eng;
  eng.retime(tl, 0);
  (void)drain(eng, 0, 50000);

  tl.playing = 0;
  eng.retime(tl, 50000);
  const auto evs = drain(eng, 50000, 200000);
  REQUIRE_FALSE(evs.empty());
  CHECK(evs[0].kind == neon::midi::EventKind::Stop);
  for (size_t i = 1; i < evs.size(); ++i) {
    CHECK(evs[i].kind != neon::midi::EventKind::Clock);
  }
}

TEST_CASE("tempo change retimes the next clock onto the new grid") {
  auto tl = snapshot(120000, 0.0, 0);
  neon::midi::ClockEngine eng;
  eng.retime(tl, 0);
  (void)drain(eng, 0, 10000);

  // Double tempo at t=10000; the next clock must be closer than 120 BPM.
  tl.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(240000);
  tl.origin_us = 10000;
  tl.beat_at_origin_q32 = neon::beat_at_q32(snapshot(120000, 0.0, 0), 10000);
  eng.retime(tl, 10000);
  const int64_t next = neon::midi::next_clock_us(tl, 10000);
  CHECK(next - 10000 <= (250000 / 24) + 2);
}

TEST_CASE("nudge shifts every emitted event by the same offset") {
  const auto tl = snapshot(120000, 0.0, 0);
  neon::midi::ClockEngine eng;
  eng.set_nudge(1500);
  eng.retime(tl, 0);
  const auto evs = drain(eng, 0, 50000);
  REQUIRE_FALSE(evs.empty());
  neon::midi::ClockEngine plain;
  plain.retime(tl, 0);
  const auto raw = drain(plain, 0, 50000);
  REQUIRE(raw.size() == evs.size());
  for (size_t i = 0; i < evs.size(); ++i) {
    CHECK(evs[i].kind == raw[i].kind);
    CHECK(evs[i].t_us == raw[i].t_us + 1500);
  }
}
