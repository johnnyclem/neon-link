#include <doctest.h>

#include <cstdint>

#include "neon/midi/sync_follower.hpp"

namespace {

using neon::midi::SessionView;
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

TEST_CASE("playing: a peer tempo edit is level-corrected, rate-limited") {
  SyncFollower f;
  SessionView s;
  s.valid = true;
  s.peers = 1;

  int64_t t = feed_ticks(f, 0, 48);
  send(f, SyncEvent::Kind::kStart, t + 1000);
  t = feed_ticks(f, t + kTick120, 24);
  auto a = f.poll(t, true, s);
  REQUIRE(a.set_tempo);
  s.tempo_mbpm = a.tempo_mbpm;  // session converges on our publish
  s.playing = true;
  const int64_t t_pub = t;

  // Session in agreement: quiet.
  t = feed_ticks(f, t + kTick120, 24);
  a = f.poll(t, true, s);
  CHECK_FALSE(a.set_tempo);

  // A peer edits the session to 150 BPM. While the MIDI transport runs
  // the sender is authoritative (spike §8.4): the follower re-asserts
  // its tempo — but never inside the rate-limit gap.
  s.tempo_mbpm = 150000;
  a = f.poll(t + 1000, true, s);
  CHECK_FALSE(a.set_tempo);

  bool corrected = false;
  for (int i = 0; i < 6 && !corrected; ++i) {
    t = feed_ticks(f, t + kTick120, 24);
    a = f.poll(t, true, s);
    if (a.set_tempo) {
      corrected = true;
      CHECK(t - t_pub >= SyncFollower::kTempoGapUs);
      CHECK(a.tempo_mbpm >= 119900);
      CHECK(a.tempo_mbpm <= 120100);
    }
  }
  REQUIRE(corrected);
}

TEST_CASE("stopped: peer edits stand; a MIDI tempo move still writes") {
  SyncFollower f;
  SessionView s;
  s.valid = true;
  s.peers = 1;
  s.playing = true;  // peers are free to run their own transport

  int64_t t = feed_ticks(f, 0, 48);  // free-running clock: not playing
  auto a = f.poll(t, true, s);
  REQUIRE(a.set_tempo);
  s.tempo_mbpm = a.tempo_mbpm;

  // A peer edits the session tempo. The MIDI transport is stopped, so
  // only edges write: the edit stands, and the peers' running transport
  // is left alone.
  s.tempo_mbpm = 150000;
  for (int i = 0; i < 4; ++i) {
    t = feed_ticks(f, t + kTick120, 24);
    a = f.poll(t, true, s);
    CHECK_FALSE(a.set_tempo);
    CHECK_FALSE(a.set_playing);
  }

  // A genuine tempo move on the MIDI side is still an edge write.
  bool republished = false;
  for (int i = 0; i < 8 && !republished; ++i) {
    t = feed_ticks(f, t + 25000, 14, 25000);  // 100 BPM
    a = f.poll(t, true, s);
    if (a.set_tempo) {
      republished = true;
      CHECK(a.tempo_mbpm >= 99000);
      CHECK(a.tempo_mbpm <= 121000);  // may fire mid-slew
    }
  }
  REQUIRE(republished);
}

TEST_CASE("playing: a peer stop is corrected while the sender runs") {
  SyncFollower f;
  SessionView s;
  s.valid = true;
  s.peers = 1;

  int64_t t = feed_ticks(f, 0, 48);
  send(f, SyncEvent::Kind::kStart, t + 1000);
  t = feed_ticks(f, t + kTick120, 1);
  auto a = f.poll(t, true, s);
  REQUIRE(a.set_playing);
  CHECK(a.playing);
  REQUIRE(a.set_tempo);
  s.tempo_mbpm = a.tempo_mbpm;
  s.playing = true;

  t = feed_ticks(f, t + kTick120, 24);
  a = f.poll(t, true, s);
  CHECK_FALSE(a.set_playing);

  // A peer stops the session under a running MIDI transport: no edge on
  // the MIDI side, but the sender is authoritative — held, rate-limited.
  s.playing = false;
  t = feed_ticks(f, t + kTick120, 24);
  a = f.poll(t, true, s);
  REQUIRE(a.set_playing);
  CHECK(a.playing);
  const int64_t t_hold = t;

  t = feed_ticks(f, t + kTick120, 4);  // ~80 ms later: inside the gap
  a = f.poll(t, true, s);
  CHECK_FALSE(a.set_playing);

  t = feed_ticks(f, t + kTick120, 48);  // past the gap, still stopped
  a = f.poll(t, true, s);
  REQUIRE(a.set_playing);
  CHECK(a.playing);
  CHECK(t - t_hold >= SyncFollower::kTempoGapUs);
}

TEST_CASE("require_peer: silent while alone, warm handover on a peer") {
  SyncFollower f;
  f.set_require_peer(true);
  SessionView s;
  s.valid = true;
  s.peers = 0;

  int64_t t = feed_ticks(f, 0, 48);
  send(f, SyncEvent::Kind::kStart, t + 1000);
  t = feed_ticks(f, t + kTick120, 24);
  const int64_t downbeat_true = t - 23 * kTick120;

  // Alone there is nothing to bridge: every write is held.
  auto a = f.poll(t, true, s);
  CHECK_FALSE(a.following);
  CHECK_FALSE(a.set_tempo);
  CHECK_FALSE(a.anchor_downbeat);
  CHECK_FALSE(a.set_playing);

  // A peer joins: republish from the warm estimate, downbeat included.
  s.peers = 1;
  t = feed_ticks(f, t + kTick120, 24);
  a = f.poll(t, true, s);
  CHECK(a.following);
  REQUIRE(a.set_tempo);
  CHECK(a.tempo_mbpm >= 119900);
  CHECK(a.tempo_mbpm <= 120100);
  REQUIRE(a.anchor_downbeat);
  CHECK(a.downbeat_us >= downbeat_true - 500);
  CHECK(a.downbeat_us <= downbeat_true + 500);
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
