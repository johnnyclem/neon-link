#include <doctest.h>

#include "neon/fixed_math.hpp"
#include "neon/transport.hpp"

namespace {

neon::TimelineSnapshot snapshot(uint32_t milli_bpm, double beat_at_origin,
                                int64_t origin_us, uint32_t quantum = 4) {
  neon::TimelineSnapshot tl;
  tl.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(milli_bpm);
  tl.origin_us = origin_us;
  tl.beat_at_origin_q32 = static_cast<int64_t>(beat_at_origin * 4294967296.0);
  tl.quantum_beats = quantum;
  tl.playing = 1;
  return tl;
}

}  // namespace

TEST_CASE("beat/time conversions are inverses on the grid") {
  const auto tl = snapshot(128000, 3.5, 1000000);
  for (int beat = -8; beat <= 16; ++beat) {
    const int64_t q32 = static_cast<int64_t>(beat) * 4294967296ll;
    const int64_t t = neon::time_at_beat_q32(tl, q32);
    // Round-trip within a microsecond of beat resolution.
    const int64_t back = neon::beat_at_q32(tl, t);
    CHECK(back >= q32 - 65536);
    CHECK(back <= q32 + 65536);
  }
}

TEST_CASE("next loop boundary lands on a quantum multiple, strictly ahead") {
  // 120 BPM, quantum 4 -> a loop every 2 s, beat 0 at t=0.
  const auto tl = snapshot(120000, 0.0, 0);
  CHECK(neon::next_loop_boundary_us(tl, 0) == 2000000);
  CHECK(neon::next_loop_boundary_us(tl, 1) == 2000000);
  CHECK(neon::next_loop_boundary_us(tl, 1999999) == 2000000);
  CHECK(neon::next_loop_boundary_us(tl, 2000000) == 4000000);
  // Before the origin the same rule applies going backwards.
  CHECK(neon::next_loop_boundary_us(tl, -1) == 0);
  CHECK(neon::next_loop_boundary_us(tl, -2000001) == -2000000);
}

TEST_CASE("loop boundary follows the loop size") {
  const auto tl = snapshot(120000, 0.0, 0, /*quantum=*/8);
  CHECK(neon::next_loop_boundary_us(tl, 0) == 4000000);
  CHECK(neon::next_loop_boundary_us(tl, 3999999) == 4000000);
}

TEST_CASE("tap tempo needs two taps and then averages the run") {
  neon::TapTempo tap;
  uint32_t bpm = 0;
  CHECK_FALSE(tap.tap(0, &bpm));  // first tap only anchors

  // 120 BPM = 500 ms between taps.
  REQUIRE(tap.tap(500000, &bpm));
  CHECK(bpm == 120000);
  REQUIRE(tap.tap(1000000, &bpm));
  CHECK(bpm == 120000);

  // A slightly late tap pulls the average, it does not jump to it.
  REQUIRE(tap.tap(1520000, &bpm));
  CHECK(bpm < 120000);
  CHECK(bpm > 115000);
}

TEST_CASE("tap tempo restarts after a long gap instead of averaging it in") {
  neon::TapTempo tap;
  uint32_t bpm = 0;
  CHECK_FALSE(tap.tap(0, &bpm));
  REQUIRE(tap.tap(500000, &bpm));
  CHECK(bpm == 120000);

  // Ten seconds later: too long to be part of the same run.
  CHECK_FALSE(tap.tap(10500000, &bpm));
  // The run restarts cleanly from there at a new tempo.
  REQUIRE(tap.tap(10750000, &bpm));
  CHECK(bpm == 240000);
}

TEST_CASE("tap tempo ignores intervals outside the tempo range") {
  neon::TapTempo tap;
  uint32_t bpm = 0;
  CHECK_FALSE(tap.tap(0, &bpm));
  // 1 ms apart would be 60000 BPM — treated as a new anchor, not a tempo.
  CHECK_FALSE(tap.tap(1000, &bpm));
  REQUIRE(tap.tap(501000, &bpm));
  CHECK(bpm == 120000);
}

TEST_CASE("milli-BPM from a double rounds instead of truncating") {
  CHECK(neon::milli_bpm_from_bpm(120.0) == 120000);
  CHECK(neon::milli_bpm_from_bpm(33.0) == 33000);
  CHECK(neon::milli_bpm_from_bpm(32.8) == 32800);
  CHECK(neon::milli_bpm_from_bpm(32.85) == 32850);
  CHECK(neon::milli_bpm_from_bpm(32.89) == 32890);
  CHECK(neon::milli_bpm_from_bpm(32.894) == 32894);
  CHECK(neon::milli_bpm_from_bpm(32.8999) == 32900);
}

TEST_CASE("milli-BPM from µs-per-beat rounds so 33.0 survives the trip") {
  CHECK(neon::milli_bpm_from_mpb_us(500000) == 120000);
  // 33 BPM = 60e6/33 ≈ 1 818 181.82 µs. Nearest integer period is
  // 1 818 182; truncating 60e9/1818182 gives 32999 (32.9 on the hero).
  CHECK(neon::milli_bpm_from_mpb_us(1818182) == 33000);
  const uint64_t q32 = neon::micros_per_beat_q32_from_milli_bpm(33000);
  const uint64_t mpb = (q32 + (1ull << 31)) >> 32;
  CHECK(neon::milli_bpm_from_mpb_us(mpb) == 33000);
  const uint64_t q120 = neon::micros_per_beat_q32_from_milli_bpm(120000);
  CHECK(neon::milli_bpm_from_mpb_us((q120 + (1ull << 31)) >> 32) == 120000);
}

TEST_CASE("tempo edits clamp at the ends of the range") {
  CHECK(neon::nudge_milli_bpm(120000, 5) == 125000);
  CHECK(neon::nudge_milli_bpm(120000, -5) == 115000);
  CHECK(neon::nudge_milli_bpm(neon::kMinMilliBpm, -50) == neon::kMinMilliBpm);
  CHECK(neon::nudge_milli_bpm(neon::kMaxMilliBpm, 50) == neon::kMaxMilliBpm);
  CHECK(neon::double_milli_bpm(90000) == 180000);
  CHECK(neon::halve_milli_bpm(90000) == 45000);
  // Halving below the floor clamps rather than wrapping to silence.
  CHECK(neon::halve_milli_bpm(neon::kMinMilliBpm) == neon::kMinMilliBpm);
  CHECK(neon::double_milli_bpm(neon::kMaxMilliBpm) == neon::kMaxMilliBpm);
}

TEST_CASE("playing_at follows the scheduled start/stop timestamp") {
  // Stopped at t=0 (Link's default): stays stopped after that instant.
  CHECK_FALSE(neon::playing_at(false, 0, 1));
  // Scheduled stop at T: still playing until T, then stopped.
  CHECK(neon::playing_at(false, 2000000, 1999999));
  CHECK_FALSE(neon::playing_at(false, 2000000, 2000000));
  CHECK_FALSE(neon::playing_at(false, 2000000, 2000001));
  // Scheduled start at T: silent until T, then playing.
  CHECK_FALSE(neon::playing_at(true, 2000000, 1999999));
  CHECK(neon::playing_at(true, 2000000, 2000000));
  CHECK(neon::playing_at(true, 2000000, 2000001));
}

TEST_CASE("quantized transport fires at the loop boundary, not on the press") {
  const auto tl = snapshot(120000, 0.0, 0);
  neon::TransportLatch latch;
  bool play = false;

  latch.request(tl, 100000, true);
  CHECK(latch.armed());
  CHECK(latch.fire_at_us() == 2000000);
  CHECK_FALSE(latch.poll(1999999, &play));
  REQUIRE(latch.poll(2000000, &play));
  CHECK(play);
  CHECK_FALSE(latch.armed());
  // Firing is one-shot.
  CHECK_FALSE(latch.poll(3000000, &play));
}

TEST_CASE("unquantized transport fires immediately") {
  const auto tl = snapshot(120000, 0.0, 0);
  neon::TransportLatch latch;
  bool play = true;
  latch.request(tl, 123456, false, /*quantized=*/false);
  REQUIRE(latch.poll(123456, &play));
  CHECK_FALSE(play);
}

TEST_CASE("a second request replaces the pending one") {
  const auto tl = snapshot(120000, 0.0, 0);
  neon::TransportLatch latch;
  bool play = false;
  latch.request(tl, 100000, true);
  latch.request(tl, 100000, false);
  REQUIRE(latch.poll(2000000, &play));
  CHECK_FALSE(play);
}

TEST_CASE("cancel disarms a pending transport change") {
  const auto tl = snapshot(120000, 0.0, 0);
  neon::TransportLatch latch;
  bool play = false;
  latch.request(tl, 100000, true);
  latch.cancel();
  CHECK_FALSE(latch.poll(9000000, &play));
}

TEST_CASE("resync targets now or the next downbeat") {
  const auto tl = snapshot(120000, 0.0, 0);
  CHECK(neon::resync_target_us(tl, 700000, neon::ResyncMode::kNow) == 700000);
  CHECK(neon::resync_target_us(tl, 700000, neon::ResyncMode::kNextLoop) ==
        2000000);
}

TEST_CASE("a zero-tempo timeline degrades safely instead of dividing by zero") {
  neon::TimelineSnapshot tl;  // tempo_mpb_q32 == 0
  tl.origin_us = 500;
  CHECK(neon::beat_at_q32(tl, 1000000) == tl.beat_at_origin_q32);
  CHECK(neon::time_at_beat_q32(tl, 1 << 20) == 500);

  neon::TransportLatch latch;
  bool play = false;
  latch.request(tl, 900, true);
  REQUIRE(latch.poll(900, &play));  // falls back to immediate
  CHECK(play);
}

TEST_CASE("beat math is exact far from the snapshot origin") {
  // Regression: the Q32.32 numerator must be dt << 64, not dt << 32.
  // With the short numerator beat_at() collapses to beat_at_origin and
  // every loop boundary lands on the same wrong instant.
  const auto tl = snapshot(120000, 0.0, 0);
  CHECK(neon::beat_at_q32(tl, 500000) == (1ll << 32));   // half a second
  CHECK(neon::beat_at_q32(tl, 1000000) == (2ll << 32));
  CHECK(neon::beat_at_q32(tl, 250000) == (1ll << 31));   // an eighth note

  // A snapshot captured minutes ago still resolves the current beat.
  const auto old = snapshot(120000, 0.0, 0);
  CHECK(neon::beat_at_q32(old, 600000000) == (1200ll << 32));
  CHECK(neon::next_loop_boundary_us(old, 600000001) == 602000000);
}
