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

namespace {

// BLE burst delivery: ticks reach the host at connection-event
// boundaries (15 ms comb); a conforming sender stamps each with its own
// millisecond clock, mod 8192.
void feed_ble_tick(SyncFollower& f, int64_t sender_us, bool good_stamps) {
  constexpr int64_t kConnIntervalUs = 15000;
  SyncEvent ev;
  ev.kind = SyncEvent::Kind::kTick;
  ev.transport = neon::MidiClockPll::Transport::kBle;
  ev.t_us = (sender_us / kConnIntervalUs + 1) * kConnIntervalUs;
  const int64_t stamped = good_stamps ? sender_us : ev.t_us;
  ev.sender_ms13 = static_cast<uint16_t>((stamped / 1000) % 8192);
  f.on_event(ev);
}

}  // namespace

TEST_CASE("BLE with decoded sender stamps locks like a wired source") {
  SyncFollower f;
  int64_t t = 0;
  for (int i = 0; i < 480; ++i) {
    t = i * kTick120;
    feed_ble_tick(f, t, true);
  }
  // The mapper earned trust, so the PLL ran the kUsb loop on recovered
  // sender times — honest lock, tight tempo, despite the arrival comb.
  CHECK(f.ble_mapper().trusted());
  CHECK(f.pll().transport() == neon::MidiClockPll::Transport::kUsb);
  CHECK(f.pll().locked());
  CHECK(f.pll().tempo_milli_bpm() >= 119400);
  CHECK(f.pll().tempo_milli_bpm() <= 120600);
  auto a = f.poll(t + 20000, true);
  CHECK(a.following);
}

TEST_CASE("degenerate BLE stamps fall back to the raw-arrival mode") {
  SyncFollower f;
  for (int i = 0; i < 480; ++i) {
    feed_ble_tick(f, i * kTick120, false);
  }
  // Stamps that mirror the arrival buckets add nothing: the mapper never
  // trusts them and the PLL stays in its burst-hardened degraded mode —
  // tempo good to a few percent, lock honestly withheld.
  CHECK_FALSE(f.ble_mapper().trusted());
  CHECK(f.pll().transport() == neon::MidiClockPll::Transport::kBle);
  CHECK(f.pll().valid());
  CHECK_FALSE(f.pll().locked());
  CHECK(f.pll().tempo_milli_bpm() >= 116400);
  CHECK(f.pll().tempo_milli_bpm() <= 123600);
}
