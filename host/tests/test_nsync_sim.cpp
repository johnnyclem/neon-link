// Multi-peer soak: the host-side stand-in for the studio-mode bench
// (docs/STUDIO_MODE_RESULTS.md). Five nodes on wildly skewed, drifting
// clocks over a lossy jittery segment must converge to one session and
// hold pairwise phase error inside the same envelope we accept from Link.

#include <doctest.h>

#include <cstdlib>

#include "fake_net.hpp"
#include "nsync/node.hpp"

using fakenet::ClockModel;
using fakenet::FakeNet;
using nsync::Node;
using nsync::NodeConfig;

namespace {

NodeConfig cfg_for(uint64_t id) {
  NodeConfig c;
  c.node_id = id;
  return c;
}

int64_t max_pairwise_error_us(FakeNet& net, size_t n) {
  int64_t worst = 0;
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      const int64_t e = std::llabs(net.phase_error_us(i, j));
      if (e > worst) {
        worst = e;
      }
    }
  }
  return worst;
}

}  // namespace

TEST_CASE("sim: ideal segment converges to tens of µs") {
  FakeNet net(11);
  Node a(cfg_for(1)), b(cfg_for(2)), c(cfg_for(3));
  net.add_node(&a, ClockModel{0, 0});
  net.add_node(&b, ClockModel{123456789, 0});
  net.add_node(&c, ClockModel{-987654321, 0});
  a.start(120.0, net.local_time(0));
  b.start(120.0, net.local_time(1));
  c.start(120.0, net.local_time(2));
  net.run_for(10000000);
  CHECK(max_pairwise_error_us(net, 3) < 50);
}

TEST_CASE("sim: studio-grade segment holds the ±500 µs acceptance bar") {
  // A realistic quiet studio rig: sub-ms typical WiFi jitter, light loss,
  // ESP32-class crystals (±10 ppm typical, ±20 ppm worst-case). This is
  // the scenario docs/NEON_SYNC.md holds to the studio-mode target; the
  // torture case below bounds degradation beyond spec.
  FakeNet net(21);
  net.params.base_delay_us = 500;
  net.params.jitter_us = 1000;
  net.params.loss_pct = 5;

  Node a(cfg_for(0xA1)), b(cfg_for(0xB2)), c(cfg_for(0xC3)),
      d(cfg_for(0xD4)), e(cfg_for(0xE5));
  net.add_node(&a, ClockModel{0, 10});
  net.add_node(&b, ClockModel{7200000000ll, -15});
  net.add_node(&c, ClockModel{-14000000, 20});
  net.add_node(&d, ClockModel{31000000, -20});
  net.add_node(&e, ClockModel{999999, 5});
  for (size_t i = 0; i < 5; ++i) {
    net.node(i).start(100.0 + 5.0 * static_cast<double>(i),
                      net.local_time(i));
  }

  net.run_for(15000000);
  for (size_t i = 1; i < 5; ++i) {
    CHECK(net.node(i).session_id() == net.node(0).session_id());
    CHECK(net.tempo_bpm(i) == doctest::Approx(net.tempo_bpm(0)));
  }

  int64_t worst = 0;
  for (int step = 0; step < 120; ++step) {
    net.run_for(500000);
    const int64_t e = max_pairwise_error_us(net, 5);
    if (e > worst) {
      worst = e;
    }
  }
  INFO("worst pairwise phase error: ", worst, " µs");
  CHECK(worst < 500);
}

TEST_CASE("sim: torture segment (2 ms jitter, 10% loss, ±50 ppm) degrades "
          "gracefully") {
  // Deliberately beyond the shipping rig and beyond crystal spec: uniform
  // 0–2 ms of jitter on *every* packet both ways, 10% loss, ±50 ppm
  // drift. The bar here is bounded degradation, not the studio target —
  // the reference numbers for real hardware come from the studio-mode
  // bench (docs/STUDIO_MODE_RESULTS.md), not this worst case.
  FakeNet net(12);
  net.params.base_delay_us = 800;
  net.params.jitter_us = 2000;
  net.params.loss_pct = 10;

  Node a(cfg_for(0xA1)), b(cfg_for(0xB2)), c(cfg_for(0xC3)),
      d(cfg_for(0xD4)), e(cfg_for(0xE5));
  // Boot offsets of seconds-to-hours, crystals off by up to ±50 ppm.
  net.add_node(&a, ClockModel{0, 20});
  net.add_node(&b, ClockModel{7200000000ll, -35});
  net.add_node(&c, ClockModel{-14000000, 50});
  net.add_node(&d, ClockModel{31000000, -50});
  net.add_node(&e, ClockModel{999999, 5});
  for (size_t i = 0; i < 5; ++i) {
    net.node(i).start(100.0 + 5.0 * static_cast<double>(i),
                      net.local_time(i));
  }

  net.run_for(15000000);  // settle: discovery, join bursts, first slews

  // One session, one tempo.
  for (size_t i = 1; i < 5; ++i) {
    CHECK(net.node(i).session_id() == net.node(0).session_id());
    CHECK(net.tempo_bpm(i) == doctest::Approx(net.tempo_bpm(0)));
  }

  // 60 simulated seconds of steady state: sample the phase error every
  // 500 ms; the worst pair must stay inside ±800 µs (≈ 1/3 of a 32nd
  // note at 120 BPM) even out here.
  int64_t worst = 0;
  for (int step = 0; step < 120; ++step) {
    net.run_for(500000);
    const int64_t e = max_pairwise_error_us(net, 5);
    if (e > worst) {
      worst = e;
    }
  }
  INFO("worst pairwise phase error: ", worst, " µs");
  CHECK(worst < 800);
}

TEST_CASE("sim: mid-session tempo change re-converges under loss") {
  FakeNet net(13);
  net.params.jitter_us = 1500;
  net.params.loss_pct = 20;
  Node a(cfg_for(1)), b(cfg_for(2)), c(cfg_for(3));
  net.add_node(&a, ClockModel{0, 10});
  net.add_node(&b, ClockModel{5000000, -10});
  net.add_node(&c, ClockModel{-5000000, 30});
  a.start(120.0, net.local_time(0));
  b.start(120.0, net.local_time(1));
  c.start(120.0, net.local_time(2));
  net.run_for(10000000);

  a.set_tempo(128.0, net.local_time(0));
  net.run_for(4000000);
  CHECK(net.tempo_bpm(1) == doctest::Approx(128.0));
  CHECK(net.tempo_bpm(2) == doctest::Approx(128.0));
  CHECK(max_pairwise_error_us(net, 3) < 500);
}

TEST_CASE("sim: TSF fast path locks two same-AP peers tight") {
  FakeNet net(14);
  net.params.base_delay_us = 1000;
  net.params.jitter_us = 4000;  // ugly measured path...
  const uint8_t bssid[6] = {1, 2, 3, 4, 5, 6};
  net.enable_shared_tsf(bssid, 555000000);  // ...but a shared beacon clock

  Node a(cfg_for(1)), b(cfg_for(2));
  net.add_node(&a, ClockModel{0, 25});
  net.add_node(&b, ClockModel{60000000, -25});
  a.start(120.0, net.local_time(0));
  b.start(120.0, net.local_time(1));
  net.run_for(10000000);

  CHECK(a.tsf_active(net.local_time(0)));
  int64_t worst = 0;
  for (int step = 0; step < 40; ++step) {
    net.run_for(500000);
    const int64_t e = std::llabs(net.phase_error_us(0, 1));
    if (e > worst) {
      worst = e;
    }
  }
  INFO("worst TSF-path phase error: ", worst, " µs");
  CHECK(worst < 300);
}

TEST_CASE("sim: two islands merge onto the most recent human action") {
  FakeNet net(15);
  Node a(cfg_for(1)), b(cfg_for(2)), c(cfg_for(3)), d(cfg_for(4));
  net.add_node(&a, ClockModel{0, 0});
  net.add_node(&b, ClockModel{1000000, 0});
  net.add_node(&c, ClockModel{-2000000, 0});
  net.add_node(&d, ClockModel{3000000, 0});
  net.set_partition(2, true);  // {a,b} | {c,d}
  for (size_t i = 0; i < 4; ++i) {
    net.node(i).start(120.0, net.local_time(i));
  }
  net.run_for(5000000);
  CHECK(a.session_id() == b.session_id());
  CHECK(c.session_id() == d.session_id());
  CHECK(a.session_id() != c.session_id());

  // The island of {c,d} gets a human tempo edit; then the wall drops.
  c.set_tempo(93.0, net.local_time(2));
  net.run_for(2000000);
  net.set_partition(2, false);
  net.run_for(5000000);

  for (size_t i = 0; i < 4; ++i) {
    CHECK(net.tempo_bpm(i) == doctest::Approx(93.0));
    CHECK(net.node(i).session_id() == a.session_id());
    CHECK(net.node(i).num_peers(net.local_time(i)) == 3);
  }
  CHECK(max_pairwise_error_us(net, 4) < 500);
}
