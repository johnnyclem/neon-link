#include <doctest.h>

#include "nsync/peer_clock.hpp"

using nsync::PeerClock;

TEST_CASE("peer_clock: min-RTT quartile median rejects asymmetric outliers") {
  PeerClock c;
  int64_t now = 0;
  // True offset 1000 µs. Low-RTT rounds measure it well; high-RTT rounds
  // carry heavy asymmetric error, like WiFi under load.
  for (int i = 0; i < 8; ++i) {
    now += 2000000;
    c.add_measured(1000 + (i % 2 == 0 ? 20 : -20), 4000 + i, now);
    c.add_measured(9000, 80000, now);  // junk with a fat round trip
  }
  REQUIRE(c.valid(now));
  CHECK(c.offset_us(now) >= 900);
  CHECK(c.offset_us(now) <= 1100);
  CHECK(c.best_rtt_us() == 4000);
}

TEST_CASE("peer_clock: drift is projected out to now") {
  PeerClock c;
  // Offset grows 100 µs/s (100 ppm relative drift), samples every 2 s
  // over a minute. A plain median would lag ~3 ms; the detrended
  // estimate must land near the current true offset.
  int64_t now = 0;
  for (int i = 0; i < 30; ++i) {
    now = static_cast<int64_t>(i) * 2000000;
    const int64_t true_off = now / 10000;  // 100 µs per second
    c.add_measured(true_off + (i % 3 - 1) * 30, 5000 + (i % 5) * 100, now);
  }
  const int64_t true_now = now / 10000;
  const int64_t est = c.offset_us(now);
  CHECK(est > true_now - 300);
  CHECK(est < true_now + 300);
}

TEST_CASE("peer_clock: samples expire") {
  PeerClock c;
  c.add_measured(500, 1000, 0);
  CHECK(c.valid(1000000));
  CHECK_FALSE(c.valid(PeerClock::kSampleTtlUs + 1000000));
}

TEST_CASE("peer_clock: absurd RTTs are discarded") {
  PeerClock c;
  c.add_measured(123, PeerClock::kMaxUsableRttUs + 1, 0);
  c.add_measured(123, -5, 0);
  CHECK_FALSE(c.valid(0));
}

TEST_CASE("peer_clock: TSF beats the measured path while fresh") {
  PeerClock c;
  int64_t now = 0;
  for (int i = 0; i < 4; ++i) {
    now += 2000000;
    c.add_measured(5000, 3000, now);  // measured path says 5 ms
    c.add_tsf(1000 + (i % 2), now);   // TSF says 1 ms
  }
  CHECK(c.using_tsf(now));
  CHECK(c.offset_us(now) >= 999);
  CHECK(c.offset_us(now) <= 1001);
  // 15 s later the TSF samples are stale; the measured path takes over.
  now += 15000000;
  CHECK_FALSE(c.using_tsf(now));
  CHECK(c.offset_us(now) == 5000);
}

TEST_CASE("peer_clock: one TSF sample is not enough") {
  PeerClock c;
  c.add_tsf(1000, 0);
  CHECK_FALSE(c.using_tsf(0));
}

TEST_CASE("peer_clock: seed serves until a real sample lands") {
  PeerClock c;
  CHECK_FALSE(c.valid(0));
  c.seed(2500);
  CHECK(c.valid(0));
  CHECK(c.offset_us(0) == 2500);
  // The seed never overrides data.
  c.add_measured(100, 2000, 0);
  CHECK(c.offset_us(0) == 100);
  // And a second seed after data is ignored.
  c.seed(9999);
  CHECK(c.offset_us(0) == 100);
}
