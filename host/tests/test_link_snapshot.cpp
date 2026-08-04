#include <doctest.h>

#include "neon/link_snapshot.hpp"

namespace {

hal::LinkState state_at(double bpm, double beat, int64_t origin,
                        bool playing = true, uint32_t peers = 1) {
  hal::LinkState s;
  s.tempo_bpm = bpm;
  s.beat_at_origin = beat;
  s.origin_us = origin;
  s.quantum = 4.0;
  s.playing = playing;
  s.num_peers = peers;
  return s;
}

}  // namespace

TEST_CASE("build_snapshot: exact conversion at 120 BPM") {
  neon::TimelineSnapshot snap;
  CHECK(neon::build_snapshot(state_at(120.0, 8.0, 1000000), nullptr, snap));
  CHECK(snap.tempo_mpb_q32 == (500000ull << 32));
  CHECK(snap.beat_at_origin_q32 == (8ll << 32));
  CHECK(snap.origin_us == 1000000);
  CHECK(snap.playing == 1);
  CHECK(snap.quantum_beats == 4);
}

TEST_CASE("build_snapshot: clamps absurd tempi") {
  neon::TimelineSnapshot snap;
  CHECK(neon::build_snapshot(state_at(0.0, 0.0, 0), nullptr, snap));
  CHECK((snap.tempo_mpb_q32 >> 32) == 60000000ull);  // clamped to 1 BPM
  CHECK(neon::build_snapshot(state_at(1e9, 0.0, 0), nullptr, snap));
  CHECK((snap.tempo_mpb_q32 >> 32) == 60060ull);  // clamped to 999 BPM
}

TEST_CASE("build_snapshot: steady session deduplicates") {
  neon::TimelineSnapshot prev;
  REQUIRE(neon::build_snapshot(state_at(120.0, 0.0, 0), nullptr, prev));

  // 10 ms later, beat advanced exactly as predicted -> immaterial.
  neon::TimelineSnapshot next;
  CHECK_FALSE(
      neon::build_snapshot(state_at(120.0, 0.02, 10000), &prev, next));

  // Tiny float noise in the beat stays immaterial.
  CHECK_FALSE(
      neon::build_snapshot(state_at(120.0, 0.02000001, 10000), &prev, next));
}

TEST_CASE("build_snapshot: material changes republish") {
  neon::TimelineSnapshot prev;
  REQUIRE(neon::build_snapshot(state_at(120.0, 0.0, 0), nullptr, prev));

  neon::TimelineSnapshot next;
  // Tempo nudge well above the 0.005 BPM threshold.
  CHECK(neon::build_snapshot(state_at(120.5, 0.02, 10000), &prev, next));
  // Phase jump: session re-synced one beat ahead of prediction.
  CHECK(neon::build_snapshot(state_at(120.0, 1.02, 10000), &prev, next));
  // Transport change.
  CHECK(neon::build_snapshot(state_at(120.0, 0.02, 10000, false), &prev, next));
  // Peer count change.
  CHECK(
      neon::build_snapshot(state_at(120.0, 0.02, 10000, true, 2), &prev, next));
}
