#include <doctest.h>

#include <cstdint>
#include <vector>

#include "neon/fixed_math.hpp"
#include "neon/multi_engine.hpp"

namespace {

neon::TimelineSnapshot snapshot_at(uint32_t milli_bpm, double beat_at_origin,
                                   int64_t origin_us, bool playing) {
  neon::TimelineSnapshot tl;
  tl.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(milli_bpm);
  tl.origin_us = origin_us;
  tl.beat_at_origin_q32 = static_cast<int64_t>(beat_at_origin * 4294967296.0);
  tl.quantum_beats = 4;
  tl.playing = playing ? 1 : 0;
  return tl;
}

std::vector<neon::Edge> collect(neon::MultiClockEngine& eng, int64_t t0,
                                int64_t t1, size_t chunk = 16) {
  std::vector<neon::Edge> all;
  std::vector<neon::Edge> buf(chunk);
  size_t n;
  do {
    n = eng.generate(t0, t1, buf.data(), buf.size());
    all.insert(all.end(), buf.begin(), buf.begin() + n);
  } while (n == chunk);
  return all;
}

size_t count_channel(const std::vector<neon::Edge>& v, uint8_t ch,
                     bool high) {
  size_t n = 0;
  for (const auto& e : v) {
    if (e.channel == ch && e.high == high) ++n;
  }
  return n;
}

}  // namespace

TEST_CASE("merged stream is time-ordered across all channels") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;  // defaults: 4/2/1/24 PPQN
  cfg.clocks[0].shuffle_pct = 30;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 0.0, 0, true), 0);
  const auto edges = collect(eng, 0, 2000000);
  REQUIRE(!edges.empty());
  int64_t last = INT64_MIN;
  for (const auto& e : edges) {
    CHECK(e.t_us >= last);
    last = e.t_us;
  }
  // One second of 120 BPM = 2 beats: 8 rises on CLK1 (4 PPQN over 2 s ->
  // 16), 48 on CLK4 (24 PPQN * 4 beats)... just verify per-channel counts.
  CHECK(count_channel(edges, neon::kChClk1, true) == 16);
  CHECK(count_channel(edges, neon::kChClk2, true) == 8);
  CHECK(count_channel(edges, neon::kChClk3, true) == 4);
  CHECK(count_channel(edges, neon::kChClk4, true) == 96);
}

TEST_CASE("transport start emits Run high and a StartOfPlay reset pulse") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  cfg.reset_mode = neon::ResetMode::kStartOfPlay;
  cfg.reset_trig_len_us = 5000;
  eng.set_config(cfg);

  eng.retime(snapshot_at(120000, 0.0, 0, false), 0);
  auto edges = collect(eng, 0, 100000);
  // Stopped and never started: no run/reset edges yet, clocks free-run.
  CHECK(count_channel(edges, neon::kChRun, true) == 0);
  CHECK(count_channel(edges, neon::kChReset, true) == 0);
  CHECK(count_channel(edges, neon::kChClk1, true) > 0);
  CHECK_FALSE(eng.run_level());

  // Transport starts at t=200000.
  eng.retime(snapshot_at(120000, 0.0, 200000, true), 200000);
  edges = collect(eng, 100000, 400000);
  REQUIRE(count_channel(edges, neon::kChRun, true) == 1);
  REQUIRE(count_channel(edges, neon::kChReset, true) == 1);
  REQUIRE(count_channel(edges, neon::kChReset, false) == 1);
  for (const auto& e : edges) {
    if (e.channel == neon::kChRun) CHECK(e.t_us == 200000);
    if (e.channel == neon::kChReset && e.high) CHECK(e.t_us == 200000);
    if (e.channel == neon::kChReset && !e.high) CHECK(e.t_us == 205000);
  }
  CHECK(eng.run_level());

  // Stop: Run falls, no further reset.
  eng.retime(snapshot_at(120000, 0.0, 600000, false), 600000);
  edges = collect(eng, 400000, 800000);
  CHECK(count_channel(edges, neon::kChRun, false) == 1);
  CHECK(count_channel(edges, neon::kChReset, true) == 0);
  CHECK_FALSE(eng.run_level());
}

TEST_CASE("EveryBar reset pulses once per quantum while playing") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  cfg.reset_mode = neon::ResetMode::kEveryBar;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 0.0, 0, true), 0);
  // 4 seconds at 120 BPM = 8 beats = 2 bars (quantum 4) + the bar at 0.
  const auto edges = collect(eng, 0, 4000001);
  const size_t rises = count_channel(edges, neon::kChReset, true);
  CHECK(rises == 3);  // bars at beats 0, 4, 8 -> t = 0, 2 s, 4 s
  for (const auto& e : edges) {
    if (e.channel == neon::kChReset && e.high) {
      CHECK(e.t_us % 2000000 == 0);
    }
  }
}

TEST_CASE("transport gating stops clocks while Run/Reset still follow") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  cfg.transport_gating = true;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 0.0, 0, false), 0);
  auto edges = collect(eng, 0, 500000);
  CHECK(count_channel(edges, neon::kChClk1, true) == 0);

  eng.retime(snapshot_at(120000, 2.0, 1000000, true), 1000000);
  edges = collect(eng, 500000, 1500000);
  CHECK(count_channel(edges, neon::kChClk1, true) > 0);
  CHECK(count_channel(edges, neon::kChRun, true) == 1);
}

TEST_CASE("latency shifts the whole merged stream") {
  neon::MultiClockEngine a;
  neon::MultiClockEngine b;
  neon::EngineConfig cfg;
  a.set_config(cfg);
  cfg.latency_us = -2500;
  b.set_config(cfg);
  a.retime(snapshot_at(120000, 0.0, 100000, true), 100000);
  b.retime(snapshot_at(120000, 0.0, 100000, true), 100000);
  // Compare over windows shifted by the latency so both see the same set
  // of underlying grid edges.
  const auto ea = collect(a, 50000, 1000000);
  const auto eb = collect(b, 50000 - 2500, 1000000 - 2500);
  REQUIRE(!ea.empty());
  REQUIRE(ea.size() == eb.size());
  for (size_t i = 0; i < eb.size(); ++i) {
    CHECK(eb[i].t_us == ea[i].t_us - 2500);
    CHECK(eb[i].channel == ea[i].channel);
    CHECK(eb[i].high == ea[i].high);
  }
}

TEST_CASE("window continuity holds for the merged stream") {
  neon::MultiClockEngine whole;
  neon::MultiClockEngine split;
  neon::EngineConfig cfg;
  cfg.clocks[1].shuffle_pct = 40;
  cfg.reset_mode = neon::ResetMode::kEveryBar;
  whole.set_config(cfg);
  split.set_config(cfg);
  whole.retime(snapshot_at(174000, 0.0, 0, true), 0);
  split.retime(snapshot_at(174000, 0.0, 0, true), 0);

  const auto big = collect(whole, 0, 2000000);
  std::vector<neon::Edge> pieces;
  for (int64_t t = 0; t < 2000000; t += 7001) {
    const int64_t t1 = t + 7001 < 2000000 ? t + 7001 : 2000000;
    const auto part = collect(split, t, t1);
    pieces.insert(pieces.end(), part.begin(), part.end());
  }
  REQUIRE(big.size() == pieces.size());
  for (size_t i = 0; i < big.size(); ++i) {
    CHECK(big[i].t_us == pieces[i].t_us);
    CHECK(big[i].channel == pieces[i].channel);
    CHECK(big[i].high == pieces[i].high);
  }
}

// --- Legacy-parity output roles --------------------------------------

TEST_CASE("Gate role holds an output high for the duration of playback") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  cfg.clocks[1].role = neon::OutputRole::kGate;
  eng.set_config(cfg);

  eng.retime(snapshot_at(120000, 0.0, 0, false), 0);
  auto edges = collect(eng, 0, 500000);
  // A gate output never emits clock pulses, playing or not.
  CHECK(count_channel(edges, neon::kChClk2, true) == 0);

  eng.retime(snapshot_at(120000, 2.0, 1000000, true), 1000000);
  edges = collect(eng, 500000, 1500000);
  REQUIRE(count_channel(edges, neon::kChClk2, true) == 1);
  CHECK(count_channel(edges, neon::kChClk2, false) == 0);

  eng.retime(snapshot_at(120000, 6.0, 3000000, false), 3000000);
  edges = collect(eng, 1500000, 3500000);
  CHECK(count_channel(edges, neon::kChClk2, false) == 1);
  CHECK(count_channel(edges, neon::kChClk2, true) == 0);
}

TEST_CASE("ResetLoop role triggers once per loop, ResetStop only on stop") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  cfg.clocks[0].role = neon::OutputRole::kResetLoop;
  cfg.clocks[0].trig_len_us = 5000;
  cfg.clocks[1].role = neon::OutputRole::kResetStart;
  cfg.clocks[2].role = neon::OutputRole::kResetStop;
  eng.set_config(cfg);

  eng.retime(snapshot_at(120000, 0.0, 0, true), 0);
  auto edges = collect(eng, 0, 4000001);
  // Quantum 4 at 120 BPM = one loop every 2 s: beats 0, 4, 8.
  CHECK(count_channel(edges, neon::kChClk1, true) == 3);
  for (const auto& e : edges) {
    if (e.channel == neon::kChClk1 && e.high) {
      CHECK(e.t_us % 2000000 == 0);
    }
  }
  // Start fired at the first retime (stopped -> playing is the transition
  // the engine sees), stop has not happened yet.
  CHECK(count_channel(edges, neon::kChClk2, true) == 1);
  CHECK(count_channel(edges, neon::kChClk3, true) == 0);

  eng.retime(snapshot_at(120000, 10.0, 5000000, false), 5000000);
  edges = collect(eng, 4000001, 6000000);
  CHECK(count_channel(edges, neon::kChClk3, true) == 1);
  CHECK(count_channel(edges, neon::kChClk3, false) == 1);
  // Reset-at-loop stops with the transport.
  CHECK(count_channel(edges, neon::kChClk1, true) == 0);
}

TEST_CASE("reset at stop fires on the dedicated RESET jack") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  cfg.reset_mode = neon::ResetMode::kAtStop;
  cfg.reset_trig_len_us = 3000;
  eng.set_config(cfg);

  eng.retime(snapshot_at(120000, 0.0, 0, true), 0);
  auto edges = collect(eng, 0, 1000000);
  CHECK(count_channel(edges, neon::kChReset, true) == 0);  // not on start

  eng.retime(snapshot_at(120000, 4.0, 2000000, false), 2000000);
  edges = collect(eng, 1000000, 3000000);
  REQUIRE(count_channel(edges, neon::kChReset, true) == 1);
  for (const auto& e : edges) {
    if (e.channel == neon::kChReset && e.high) CHECK(e.t_us == 2000000);
    if (e.channel == neon::kChReset && !e.high) CHECK(e.t_us == 2003000);
  }
}

TEST_CASE("reset_before_edge leads the reset without moving the clocks") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  cfg.reset_mode = neon::ResetMode::kEveryBar;
  cfg.reset_before_edge = true;
  cfg.reset_lead_us = 1500;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 0.0, 0, true), 0);

  const auto edges = collect(eng, -10000, 4000001);
  size_t rises = 0;
  for (const auto& e : edges) {
    if (e.channel == neon::kChReset && e.high) {
      // Bars land on 0 / 2 s / 4 s; the reset now leads each by 1.5 ms.
      CHECK((e.t_us + 1500) % 2000000 == 0);
      ++rises;
    }
    if (e.channel == neon::kChClk1 && e.high) {
      CHECK(e.t_us % 125000 == 0);  // 4 PPQN at 120 BPM, unshifted
    }
  }
  CHECK(rises == 3);
}

TEST_CASE("free_run keeps one output pulsing while gating mutes the rest") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  cfg.transport_gating = true;
  cfg.clocks[0].free_run = true;  // "Clock (Always On)"
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 0.0, 0, false), 0);

  const auto edges = collect(eng, 0, 1000000);
  CHECK(count_channel(edges, neon::kChClk1, true) > 0);
  CHECK(count_channel(edges, neon::kChClk2, true) == 0);
}

TEST_CASE("rhythm_over_loop spreads the pattern across the loop, not the grid") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  cfg.clocks[0].ppqn = 4;  // ignored once rhythm_over_loop is on
  cfg.clocks[0].rhythm = neon::ClockOutputConfig::RhythmMode::kPattern;
  cfg.clocks[0].euclid_steps = 16;
  cfg.clocks[0].step_mask = ~0ull;
  cfg.clocks[0].rhythm_over_loop = true;
  cfg.clocks[1].enabled = false;
  cfg.clocks[2].enabled = false;
  cfg.clocks[3].enabled = false;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 0.0, 0, true), 0);

  // 16 steps across a 4-beat loop at 120 BPM = 16 pulses per 2 s.
  auto edges = collect(eng, 0, 2000000);
  CHECK(count_channel(edges, neon::kChClk1, true) == 16);

  // Halving the loop packs the same 16 steps into half the time.
  neon::MultiClockEngine eng2;
  eng2.set_config(cfg);
  neon::TimelineSnapshot tl = snapshot_at(120000, 0.0, 0, true);
  tl.quantum_beats = 2;
  eng2.retime(tl, 0);
  edges = collect(eng2, 0, 2000000);
  CHECK(count_channel(edges, neon::kChClk1, true) == 32);
}

// --- Regressions -----------------------------------------------------

TEST_CASE("a leading reset still fires when the window starts at the anchor") {
  // The pulse task retimes at the cursor and then generates from that same
  // instant, so reset_before_edge puts the rise just before t0. It must be
  // clamped into the window, not dropped — dropping it left the RESET jack
  // emitting a falling edge with no rise.
  for (int lead : {0, 1500}) {
    neon::MultiClockEngine eng;
    neon::EngineConfig cfg;
    cfg.reset_mode = neon::ResetMode::kStartOfPlay;
    cfg.reset_before_edge = lead != 0;
    cfg.reset_lead_us = static_cast<uint32_t>(lead);
    cfg.clocks[0].role = neon::OutputRole::kResetStart;
    eng.set_config(cfg);

    eng.retime(snapshot_at(120000, 0.0, 0, false), 0);
    collect(eng, 0, 1000000);
    // Transport starts exactly at the window boundary.
    eng.retime(snapshot_at(120000, 2.0, 1000000, true), 1000000);
    const auto edges = collect(eng, 1000000, 2000000);

    CHECK(count_channel(edges, neon::kChReset, true) == 1);
    CHECK(count_channel(edges, neon::kChReset, false) == 1);
    CHECK(count_channel(edges, neon::kChClk1, true) == 1);
    CHECK(count_channel(edges, neon::kChClk1, false) == 1);
    for (const auto& e : edges) {
      CHECK(e.t_us >= 1000000);  // nothing escapes below the window
    }
  }
}

TEST_CASE("changing a role mid-flight drives the gate to its new level") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 0.0, 0, true), 0);
  collect(eng, 0, 500000);

  // Assign OUT2 as a gate while the transport is already playing.
  cfg.clocks[1].role = neon::OutputRole::kGate;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 2.0, 1000000, true), 1000000);
  auto edges = collect(eng, 1000000, 1500000);
  REQUIRE(count_channel(edges, neon::kChClk2, true) == 1);

  // Take the role away again: the jack must not stay stuck high.
  cfg.clocks[1].role = neon::OutputRole::kClock;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 4.0, 2000000, true), 2000000);
  edges = collect(eng, 1500000, 2500000);
  CHECK(count_channel(edges, neon::kChClk2, false) >= 1);

  // Disabling a gate output drops it low too.
  cfg.clocks[2].role = neon::OutputRole::kGate;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 6.0, 3000000, true), 3000000);
  edges = collect(eng, 2500000, 3500000);
  REQUIRE(count_channel(edges, neon::kChClk3, true) == 1);

  cfg.clocks[2].enabled = false;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 8.0, 4000000, true), 4000000);
  edges = collect(eng, 3500000, 4500000);
  CHECK(count_channel(edges, neon::kChClk3, false) == 1);
}

TEST_CASE("run gate follows run_enabled without a transport transition") {
  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  cfg.run_enabled = false;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 0.0, 0, true), 0);
  auto edges = collect(eng, 0, 500000);
  CHECK(count_channel(edges, neon::kChRun, true) == 0);
  CHECK_FALSE(eng.run_level());

  cfg.run_enabled = true;
  eng.set_config(cfg);
  eng.retime(snapshot_at(120000, 2.0, 1000000, true), 1000000);
  edges = collect(eng, 500000, 1500000);
  CHECK(count_channel(edges, neon::kChRun, true) == 1);
  CHECK(eng.run_level());
}
