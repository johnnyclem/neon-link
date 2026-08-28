#include <doctest.h>

#include <cstdint>

#include "neon/midi/sync_follower.hpp"

namespace {

using neon::midi::SyncEvent;
using neon::midi::SyncFollower;

constexpr int64_t kTick120 = 20833;  // 120.0019 BPM on an integer grid

int64_t feed_ticks(SyncFollower& f, int64_t t0, int count,
                   int64_t period = kTick120) {
  int64_t t = t0;
  for (int i = 0; i < count; ++i) {
    SyncEvent ev;
    ev.kind = SyncEvent::Kind::kTick;
    ev.t_us = t;
    f.on_event(ev);
    t += period;
  }
  return t - period;  // time of the last tick
}

void send(SyncFollower& f, SyncEvent::Kind kind, int64_t t, uint16_t spp = 0) {
  SyncEvent ev;
  ev.kind = kind;
  ev.t_us = t;
  ev.spp = spp;
  f.on_event(ev);
}

}  // namespace

TEST_CASE("follower publishes once on lock, then holds inside the band") {
  SyncFollower f;
  int64_t t = feed_ticks(f, 0, 48);

  auto a = f.poll(t, true);
  CHECK(a.following);
  REQUIRE(a.set_tempo);
  CHECK(a.tempo_mbpm >= 119900);
  CHECK(a.tempo_mbpm <= 120100);

  // Steady clock: further polls must not spam the session.
  for (int i = 0; i < 10; ++i) {
    t = feed_ticks(f, t + kTick120, 24);
    a = f.poll(t, true);
    CHECK(a.following);
    CHECK_FALSE(a.set_tempo);
  }
}

TEST_CASE("follower republishes after a real tempo step, rate-limited") {
  SyncFollower f;
  int64_t t = feed_ticks(f, 0, 96);
  auto a = f.poll(t, true);
  REQUIRE(a.set_tempo);
  const int64_t t_first = t;

  // Step to 140 BPM (period 17857 us). Give the PLL two seconds of the
  // new grid, polling as a service loop would.
  bool republished = false;
  uint32_t new_mbpm = 0;
  for (int i = 0; i < 8; ++i) {
    t = feed_ticks(f, t + 17857, 14, 17857);
    a = f.poll(t, true);
    if (a.set_tempo) {
      republished = true;
      new_mbpm = a.tempo_mbpm;
      // The rate limit keeps corrections at least a second apart.
      CHECK(t - t_first >= SyncFollower::kTempoGapUs);
    }
  }
  REQUIRE(republished);
  CHECK(new_mbpm >= 139000);
  CHECK(new_mbpm <= 141000);
}

TEST_CASE("follower forwards the downbeat and transport edges") {
  SyncFollower f;
  int64_t t = feed_ticks(f, 0, 48);
  auto a = f.poll(t, true);
  CHECK_FALSE(a.set_playing);  // free-running clock: transport untouched

  send(f, SyncEvent::Kind::kStart, t + 1000);
  t = feed_ticks(f, t + kTick120, 1);
  a = f.poll(t, true);
  REQUIRE(a.anchor_downbeat);
  CHECK(a.downbeat_us >= t - 500);
  CHECK(a.downbeat_us <= t + 500);
  REQUIRE(a.set_playing);
  CHECK(a.playing);

  t = feed_ticks(f, t + kTick120, 24);
  a = f.poll(t, true);
  CHECK_FALSE(a.set_playing);  // no edge, no action

  send(f, SyncEvent::Kind::kStop, t + 1000);
  t = feed_ticks(f, t + kTick120, 1);
  a = f.poll(t, true);
  REQUIRE(a.set_playing);
  CHECK_FALSE(a.playing);
}

TEST_CASE("not allowed: silent tracking, then a warm, anchored handover") {
  SyncFollower f;
  int64_t t = feed_ticks(f, 0, 48);
  send(f, SyncEvent::Kind::kStart, t + 1000);
  t = feed_ticks(f, t + kTick120, 24);
  const int64_t downbeat_true = t - 23 * kTick120;

  // CLK IN holds the clock: the follower must stay entirely quiet.
  auto a = f.poll(t, false);
  CHECK_FALSE(a.following);
  CHECK_FALSE(a.set_tempo);
  CHECK_FALSE(a.anchor_downbeat);
  CHECK_FALSE(a.set_playing);

  // CLK IN released: the follower takes over from a warm estimate,
  // republishes tempo, and anchors to the downbeat that fired while it
  // was waiting — a past downbeat is still the correct phase anchor for
  // a clock that kept running.
  t = feed_ticks(f, t + kTick120, 24);
  a = f.poll(t, true);
  CHECK(a.following);
  REQUIRE(a.set_tempo);
  CHECK(a.tempo_mbpm >= 119900);
  CHECK(a.tempo_mbpm <= 120100);
  REQUIRE(a.anchor_downbeat);
  CHECK(a.downbeat_us >= downbeat_true - 500);
  CHECK(a.downbeat_us <= downbeat_true + 500);
  // The transport that was already running propagates on first follow.
  REQUIRE(a.set_playing);
  CHECK(a.playing);
}

TEST_CASE("following() tracks the poll state the services arbitrate on") {
  // The link services drop the MIDI router's duplicate unquantized
  // play/stop while the follower owns transport; the flag they read
  // between polls is this sticky state, not a per-poll edge.
  SyncFollower f;
  CHECK_FALSE(f.following());

  int64_t t = feed_ticks(f, 0, 48);
  f.poll(t, true);
  CHECK(f.following());

  // CLK IN takes the clock: the very next poll releases ownership.
  f.poll(t + 1000, false);
  CHECK_FALSE(f.following());

  // CLK IN gone again: re-follow from the warm estimate.
  t = feed_ticks(f, t + kTick120, 24);
  f.poll(t, true);
  CHECK(f.following());

  // A dead clock clears ownership too.
  f.poll(t + 2000000, true);
  CHECK_FALSE(f.following());
}

TEST_CASE("clock loss stops the following state cleanly") {
  SyncFollower f;
  int64_t t = feed_ticks(f, 0, 48);
  auto a = f.poll(t, true);
  CHECK(a.following);

  a = f.poll(t + 2000000, true);  // two silent seconds
  CHECK_FALSE(a.following);
  CHECK_FALSE(a.set_tempo);

  // A returning clock at a new tempo re-follows and republishes.
  t = feed_ticks(f, t + 3000000, 16, 25000);  // 100 BPM
  a = f.poll(t, true);
  CHECK(a.following);
  REQUIRE(a.set_tempo);
  CHECK(a.tempo_mbpm >= 99900);
  CHECK(a.tempo_mbpm <= 100100);
}
