// The DAW bridge (nsync::DawFollower) under the fake segment: a simulated
// AudioPlayHead on the plugin endpoint's local clock drives the mesh, and
// the device nodes must end up on the DAW's tempo, bar grid, and transport
// — with the authority rules of docs/NEON_SYNC.md §7.4 (DAW authoritative
// while playing; edge-only while stopped; silent with no peers or with
// drive off). Also covers the audio-thread PlayheadMailbox.

#include <doctest.h>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <thread>

#include "fake_net.hpp"
#include "nsync/daw_follower.hpp"
#include "nsync/node.hpp"
#include "nsync/playhead_mailbox.hpp"

using fakenet::ClockModel;
using fakenet::FakeNet;
using nsync::DawFollower;
using nsync::DawPlayhead;
using nsync::FollowerConfig;
using nsync::Node;
using nsync::NodeConfig;
using nsync::PlayheadMailbox;

namespace {

// Firmware ids carry 'N' in the high byte; the plugin uses 'D', so a
// device always outranks the plugin and the laptop clock can never become
// the session's ghost reference.
constexpr uint64_t kDeviceA = 0x4E00000000000002ull;
constexpr uint64_t kDeviceB = 0x4E00000000000001ull;
constexpr uint64_t kPlugin = 0x4400000000000009ull;
static_assert(kPlugin < kDeviceB && kDeviceB < kDeviceA,
              "the plugin id must sort below every device id");

NodeConfig cfg_for(uint64_t id) {
  NodeConfig c;
  c.node_id = id;
  return c;
}

// A deterministic DAW transport running on one endpoint's local clock.
struct FakeDaw {
  double bpm = 124.0;
  bool playing = false;
  double beat0 = 0.0;      // PPQ where the playhead currently sits/starts
  int64_t t_start_us = 0;  // local clock at the moment playback started

  double beat_at(int64_t local_us) const {
    if (!playing) {
      return beat0;
    }
    return beat0 + static_cast<double>(local_us - t_start_us) * bpm / 60e6;
  }

  DawPlayhead sample(int64_t local_us) const {
    DawPlayhead ph;
    ph.valid = true;
    ph.playing = playing;
    ph.bpm = bpm;
    ph.beat = beat_at(local_us);
    ph.sampled_us = local_us;
    return ph;
  }

  void start(int64_t local_us, double at_beat = 0.0) {
    playing = true;
    beat0 = at_beat;
    t_start_us = local_us;
  }
  void stop(int64_t local_us) {
    beat0 = beat_at(local_us);
    playing = false;
  }
};

// Steps the segment in service-tick chunks, feeding the follower a fresh
// playhead sample each tick — the shape of the plugin's socket loop.
void run_with_daw(FakeNet& net, DawFollower& f, const FakeDaw& daw,
                  size_t plugin_idx, int64_t dur_us) {
  const int64_t end = net.now_true() + dur_us;
  while (net.now_true() < end) {
    net.run_for(10000);
    const int64_t local = net.local_time(plugin_idx);
    f.update(daw.sample(local), local);
  }
}

// Session grid vs DAW grid at one node, µs of the session tempo, wrapped
// to the nearest quantum image — the follower's own error metric, measured
// independently here.
int64_t daw_phase_err_us(FakeNet& net, size_t idx, size_t plugin_idx,
                         const FakeDaw& daw) {
  hal::LinkState st;
  REQUIRE(net.node(idx).capture(st, net.local_time(idx)));
  const double q = st.quantum > 0.0 ? st.quantum : 4.0;
  const double daw_beat = daw.beat_at(net.local_time(plugin_idx));
  double d = std::fmod(st.beat_at_origin - daw_beat, q);
  if (d >= q / 2) {
    d -= q;
  } else if (d < -q / 2) {
    d += q;
  }
  return static_cast<int64_t>(std::llround(d * 60e6 / st.tempo_bpm));
}

bool node_playing(FakeNet& net, size_t idx) {
  hal::LinkState st;
  REQUIRE(net.node(idx).capture(st, net.local_time(idx)));
  return st.playing;
}

}  // namespace

TEST_CASE("mailbox: empty, roundtrip, latest-wins") {
  PlayheadMailbox box;
  DawPlayhead out;
  CHECK(!box.read(out));

  DawPlayhead in;
  in.valid = true;
  in.playing = true;
  in.bpm = 133.25;
  in.beat = 17.75;
  in.sampled_us = -12345678;  // steady_clock epochs can sit anywhere
  box.publish(in);
  REQUIRE(box.read(out));
  CHECK(out.valid == in.valid);
  CHECK(out.playing == in.playing);
  CHECK(out.bpm == in.bpm);
  CHECK(out.beat == in.beat);
  CHECK(out.sampled_us == in.sampled_us);

  in.bpm = 90.0;
  in.playing = false;
  box.publish(in);
  REQUIRE(box.read(out));
  CHECK(out.bpm == 90.0);
  CHECK(!out.playing);
}

TEST_CASE("mailbox: concurrent reader never sees a torn sample") {
  // Writer keeps bpm == beat; any read where they differ is a tear.
  PlayheadMailbox box;
  std::atomic<bool> stop{false};
  std::thread writer([&] {
    double v = 1.0;
    while (!stop.load(std::memory_order_relaxed)) {
      DawPlayhead s;
      s.valid = true;
      s.bpm = v;
      s.beat = v;
      s.sampled_us = static_cast<int64_t>(v);
      box.publish(s);
      v += 1.0;
    }
  });
  int reads = 0;
  DawPlayhead out;
  for (int i = 0; i < 200000; ++i) {
    if (box.read(out)) {
      ++reads;
      REQUIRE(out.bpm == out.beat);
      REQUIRE(out.sampled_us == static_cast<int64_t>(out.bpm));
    }
  }
  stop.store(true);
  writer.join();
  CHECK(reads > 0);
}

TEST_CASE("follower: writes nothing while it has no peers") {
  FakeNet net(31);
  Node p(cfg_for(kPlugin));
  net.add_node(&p, ClockModel{0, 0});
  p.start(120.0, net.local_time(0));
  DawFollower f(p);

  FakeDaw daw;
  daw.bpm = 140.0;
  daw.start(net.local_time(0));
  run_with_daw(net, f, daw, 0, 3000000);

  // Alone there is nothing to sync: the state is still the founding one,
  // so the first device's announce wins the seq-1 tie and the plugin
  // adopts the session's quantum and time domain instead of imposing its
  // defaults.
  CHECK(p.state().tl.seq == 1);
  CHECK(net.tempo_bpm(0) == doctest::Approx(120.0));
}

TEST_CASE("follower: adopts the device session, then drives tempo, grid "
          "and transport") {
  FakeNet net(32);
  Node dev(cfg_for(kDeviceA));
  Node p(cfg_for(kPlugin));
  net.add_node(&dev, ClockModel{5000000, 15});
  net.add_node(&p, ClockModel{0, -10});
  dev.start(120.0, net.local_time(0));
  p.start(120.0, net.local_time(1));
  DawFollower f(p);

  FakeDaw daw;
  daw.bpm = 124.0;

  // Discovery and clock warmup with the DAW stopped: nothing is written,
  // and the mesh keeps the device's tempo.
  run_with_daw(net, f, daw, 1, 3000000);
  CHECK(p.session_id() == kDeviceA);
  CHECK(net.tempo_bpm(0) == doctest::Approx(120.0));
  CHECK(p.state().tl.writer == kDeviceA);

  // Play. The start edge re-tempos the mesh, lands a quantum boundary on
  // the DAW's bar line, and the device converges onto both.
  daw.start(net.local_time(1), 16.0);  // mid-arrangement, bar 5 in 4/4
  run_with_daw(net, f, daw, 1, 5000000);

  CHECK(net.tempo_bpm(0) == doctest::Approx(124.0));
  CHECK(p.session_id() == kDeviceA);  // still the device's session
  CHECK(node_playing(net, 0));
  // Mesh-internal coherence, and the device grid on the DAW's bars. The
  // device error budget is the plugin-device offset estimate (clean
  // segment here), not the DAW side, which shares the plugin's clock.
  CHECK(std::llabs(net.phase_error_us(0, 1)) < 500);
  CHECK(std::llabs(daw_phase_err_us(net, 1, 1, daw)) < 500);
  CHECK(std::llabs(daw_phase_err_us(net, 0, 1, daw)) < 1500);

  const auto st = f.status(net.local_time(1));
  CHECK(st.session_up);
  CHECK(st.peers == 1);
  CHECK(st.daw_playing);
  CHECK(st.phase_valid);
  CHECK(st.phase_locked);

  // Stop. The edge stops the mesh.
  daw.stop(net.local_time(1));
  run_with_daw(net, f, daw, 1, 2000000);
  CHECK(!node_playing(net, 0));
  CHECK(!node_playing(net, 1));
}

TEST_CASE("follower: corrects mesh edits while the DAW plays") {
  FakeNet net(33);
  Node dev(cfg_for(kDeviceA));
  Node p(cfg_for(kPlugin));
  net.add_node(&dev, ClockModel{-3000000, 10});
  net.add_node(&p, ClockModel{0, 0});
  dev.start(120.0, net.local_time(0));
  p.start(120.0, net.local_time(1));
  DawFollower f(p);

  FakeDaw daw;
  daw.bpm = 124.0;
  run_with_daw(net, f, daw, 1, 3000000);
  daw.start(net.local_time(1));
  run_with_daw(net, f, daw, 1, 3000000);
  CHECK(net.tempo_bpm(0) == doctest::Approx(124.0));

  // A device-side tempo edit mid-playback: the DAW cannot follow it (VST3
  // cannot set host tempo), so the follower pushes back.
  dev.set_tempo(90.0, net.local_time(0));
  run_with_daw(net, f, daw, 1, 3000000);
  CHECK(net.tempo_bpm(0) == doctest::Approx(124.0));
  CHECK(net.tempo_bpm(1) == doctest::Approx(124.0));

  // A device-side stop mid-playback is corrected the same way.
  dev.set_playing(false, net.local_time(0));
  run_with_daw(net, f, daw, 1, 3000000);
  CHECK(node_playing(net, 0));
  CHECK(node_playing(net, 1));
}

TEST_CASE("follower: mesh edits stand while the DAW is stopped; DAW tempo "
          "edits still preview as edges") {
  FakeNet net(34);
  Node dev(cfg_for(kDeviceA));
  Node p(cfg_for(kPlugin));
  net.add_node(&dev, ClockModel{1000000, 0});
  net.add_node(&p, ClockModel{0, 0});
  dev.start(120.0, net.local_time(0));
  p.start(120.0, net.local_time(1));
  DawFollower f(p);

  FakeDaw daw;
  daw.bpm = 124.0;
  run_with_daw(net, f, daw, 1, 3000000);

  // Device edit with the DAW stopped: the plugin follows, no fight.
  dev.set_tempo(101.0, net.local_time(0));
  run_with_daw(net, f, daw, 1, 3000000);
  CHECK(net.tempo_bpm(0) == doctest::Approx(101.0));
  CHECK(net.tempo_bpm(1) == doctest::Approx(101.0));

  // A DAW tempo change while stopped is an edge write.
  daw.bpm = 132.0;
  run_with_daw(net, f, daw, 1, 3000000);
  CHECK(net.tempo_bpm(0) == doctest::Approx(132.0));

  // And a device edit after that stands again.
  dev.set_tempo(97.0, net.local_time(0));
  run_with_daw(net, f, daw, 1, 3000000);
  CHECK(net.tempo_bpm(1) == doctest::Approx(97.0));
}

TEST_CASE("follower: a stale playhead releases authority without writing") {
  FakeNet net(35);
  Node dev(cfg_for(kDeviceA));
  Node p(cfg_for(kPlugin));
  net.add_node(&dev, ClockModel{0, 0});
  net.add_node(&p, ClockModel{0, 0});
  dev.start(120.0, net.local_time(0));
  p.start(120.0, net.local_time(1));
  DawFollower f(p);

  FakeDaw daw;
  daw.bpm = 124.0;
  run_with_daw(net, f, daw, 1, 3000000);
  daw.start(net.local_time(1));
  run_with_daw(net, f, daw, 1, 3000000);
  CHECK(net.tempo_bpm(0) == doctest::Approx(124.0));

  // The host's engine goes idle: the same sample keeps arriving, ages out,
  // and the follower must neither assert the DAW state nor write a stop.
  const DawPlayhead frozen = daw.sample(net.local_time(1));
  const int64_t end = net.now_true() + 2000000;
  while (net.now_true() < end) {
    net.run_for(10000);
    f.update(frozen, net.local_time(1));
  }
  CHECK(node_playing(net, 0));  // no phantom stop edge
  dev.set_tempo(95.0, net.local_time(0));
  const int64_t end2 = net.now_true() + 3000000;
  while (net.now_true() < end2) {
    net.run_for(10000);
    f.update(frozen, net.local_time(1));
  }
  CHECK(net.tempo_bpm(0) == doctest::Approx(95.0));
  CHECK(net.tempo_bpm(1) == doctest::Approx(95.0));
  CHECK(!f.status(net.local_time(1)).daw_fresh);
}

TEST_CASE("follower: drive off is a pure monitor") {
  FakeNet net(36);
  Node dev(cfg_for(kDeviceA));
  Node p(cfg_for(kPlugin));
  net.add_node(&dev, ClockModel{0, 0});
  net.add_node(&p, ClockModel{0, 0});
  dev.start(118.0, net.local_time(0));
  p.start(120.0, net.local_time(1));
  DawFollower f(p);
  f.set_drive(false);

  FakeDaw daw;
  daw.bpm = 150.0;
  daw.start(net.local_time(1));
  run_with_daw(net, f, daw, 1, 5000000);

  // The node participates (adopts the device session) but the DAW writes
  // nothing, and the status still reports the mismatch for the UI.
  CHECK(net.tempo_bpm(0) == doctest::Approx(118.0));
  CHECK(net.tempo_bpm(1) == doctest::Approx(118.0));
  CHECK(p.session_id() == kDeviceA);
  const auto st = f.status(net.local_time(1));
  CHECK(!st.drive);
  CHECK(st.daw_playing);
  CHECK(st.daw_bpm == doctest::Approx(150.0));
  CHECK(st.session_bpm == doctest::Approx(118.0));
}

TEST_CASE("follower: holds the DAW grid across a jittery lossy segment "
          "with two devices") {
  FakeNet net(37);
  net.params.base_delay_us = 500;
  net.params.jitter_us = 1000;
  net.params.loss_pct = 5;

  Node a(cfg_for(kDeviceA));
  Node b(cfg_for(kDeviceB));
  Node p(cfg_for(kPlugin));
  net.add_node(&a, ClockModel{7200000000ll, 20});
  net.add_node(&b, ClockModel{-14000000, -15});
  net.add_node(&p, ClockModel{999999, 5});
  a.start(120.0, net.local_time(0));
  b.start(120.0, net.local_time(1));
  p.start(120.0, net.local_time(2));
  DawFollower f(p);

  FakeDaw daw;
  daw.bpm = 126.5;
  run_with_daw(net, f, daw, 2, 5000000);
  daw.start(net.local_time(2), 64.0);
  run_with_daw(net, f, daw, 2, 10000000);

  CHECK(net.tempo_bpm(0) == doctest::Approx(126.5));
  CHECK(net.tempo_bpm(1) == doctest::Approx(126.5));
  CHECK(node_playing(net, 0));
  CHECK(node_playing(net, 1));

  // 20 simulated seconds of steady state: the devices stay inside the
  // studio envelope against each other and within a couple of estimator
  // error budgets of the DAW's own bar grid.
  int64_t worst_mesh = 0;
  int64_t worst_daw = 0;
  for (int step = 0; step < 40; ++step) {
    run_with_daw(net, f, daw, 2, 500000);
    worst_mesh = std::max<int64_t>(worst_mesh,
                                   std::llabs(net.phase_error_us(0, 1)));
    worst_daw = std::max<int64_t>(
        worst_daw, std::max<int64_t>(
                       std::llabs(daw_phase_err_us(net, 0, 2, daw)),
                       std::llabs(daw_phase_err_us(net, 1, 2, daw))));
  }
  INFO("worst device-device phase error: ", worst_mesh, " µs");
  INFO("worst device-DAW phase error: ", worst_daw, " µs");
  CHECK(worst_mesh < 800);
  CHECK(worst_daw < 4000);
}
