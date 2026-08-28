#include <doctest.h>

#include <cstdint>

#include "neon/midi/ble_time_mapper.hpp"

namespace {

using neon::midi::BleTimeMapper;

constexpr int64_t kTickUs = 20833;  // 120.0019 BPM at 24 PPQN
constexpr int64_t kConnIntervalUs = 15000;

// Sender-side true time of tick i.
int64_t sender_time(int i) { return static_cast<int64_t>(i) * kTickUs; }

// The stamp a conforming sender puts in the packet: its own millisecond
// clock, modulo 8.192 s.
uint16_t good_stamp(int64_t sender_us) {
  return static_cast<uint16_t>((sender_us / 1000) % 8192);
}

// Burst delivery: the byte reaches the host at the next connection-event
// boundary after it was sent — the arrival comb the raw path suffers.
int64_t comb_arrival(int64_t sender_us) {
  return (sender_us / kConnIntervalUs + 1) * kConnIntervalUs;
}

}  // namespace

TEST_CASE("clean sender stamps earn trust and recover the tick spacing") {
  BleTimeMapper m;

  // Warmup: the delta window plus the trust streak is under 30 ticks.
  int i = 0;
  for (; i < 30; ++i) {
    m.on_tick(comb_arrival(sender_time(i)), good_stamp(sender_time(i)));
  }
  CHECK(m.trusted());

  // From here every mapped delta must sit near the true period even
  // though the arrivals it rode in on are quantized to the connection
  // interval: ±1 ms of stamp granularity plus a little median wobble.
  int64_t prev = m.on_tick(comb_arrival(sender_time(i)),
                           good_stamp(sender_time(i)));
  ++i;
  int64_t sum = 0;
  const int kSpan = 100;
  for (int n = 0; n < kSpan; ++n, ++i) {
    const int64_t t = m.on_tick(comb_arrival(sender_time(i)),
                                good_stamp(sender_time(i)));
    const int64_t d = t - prev;
    CHECK(d >= kTickUs - 2500);
    CHECK(d <= kTickUs + 2500);
    sum += d;
    prev = t;
  }
  // The wobble is zero-mean: the recovered grid does not drift.
  CHECK(sum >= kSpan * kTickUs - 2000);
  CHECK(sum <= kSpan * kTickUs + 2000);
  CHECK(m.trusted());
}

TEST_CASE("the 8.192 s stamp wrap is unwrapped against packet arrival") {
  BleTimeMapper m;
  // 800 ticks at 20.833 ms crosses the 8192 ms modulus twice. Delta
  // bounds apply once consecutive outputs are both mapped times (the
  // warm-up returns raw comb arrivals, whose spacing is honestly lumpy).
  int64_t prev = 0;
  int64_t first_mapped = 0;
  int first_mapped_i = -1;
  for (int i = 0; i < 800; ++i) {
    const bool was_trusted = m.trusted();
    const int64_t t = m.on_tick(comb_arrival(sender_time(i)),
                                good_stamp(sender_time(i)));
    if (was_trusted && m.trusted()) {
      CHECK(t - prev >= 0);  // never a modulus-sized jump backwards
      CHECK(t - prev <= kTickUs + 2500);
    } else if (m.trusted() && first_mapped_i < 0) {
      first_mapped = t;
      first_mapped_i = i;
    }
    prev = t;
  }
  CHECK(m.trusted());
  REQUIRE(first_mapped_i >= 0);
  // The recovered grid spans the sender's elapsed time across both
  // wraps. (Compare spans, not absolutes — the constant delivery-latency
  // offset is invisible by design.)
  const int64_t span = prev - first_mapped;
  const int64_t true_span = sender_time(799) - sender_time(first_mapped_i);
  CHECK(span >= true_span - 20000);
  CHECK(span <= true_span + 20000);
}

TEST_CASE("degenerate stamps (arrival buckets) never earn trust") {
  BleTimeMapper m;
  for (int i = 0; i < 400; ++i) {
    const int64_t arrival = comb_arrival(sender_time(i));
    // The broken stack stamps at the connection-event bucket: the stamp
    // is just the arrival comb in sender units.
    const uint16_t stamp = good_stamp(arrival);
    const int64_t t = m.on_tick(arrival, stamp);
    CHECK(t == arrival);  // raw-arrival mode, always
    CHECK_FALSE(m.trusted());
  }
}

TEST_CASE("a sender going degenerate mid-stream loses trust") {
  BleTimeMapper m;
  for (int i = 0; i < 60; ++i) {
    m.on_tick(comb_arrival(sender_time(i)), good_stamp(sender_time(i)));
  }
  REQUIRE(m.trusted());
  // Stamps collapse onto the arrival bucket (e.g. a stack update): one
  // full window of comb-shaped spacing revokes trust.
  for (int i = 60; i < 100; ++i) {
    const int64_t arrival = comb_arrival(sender_time(i));
    m.on_tick(arrival, good_stamp(arrival));
  }
  CHECK_FALSE(m.trusted());
}

TEST_CASE("a long silence resets the mapper: raw until re-warmed") {
  BleTimeMapper m;
  for (int i = 0; i < 60; ++i) {
    m.on_tick(comb_arrival(sender_time(i)), good_stamp(sender_time(i)));
  }
  REQUIRE(m.trusted());

  // 3 s of nothing: unwrapping against arrival is guesswork, start over.
  const int64_t resume = comb_arrival(sender_time(59)) + 3000000;
  const int64_t t = m.on_tick(resume, good_stamp(resume));
  CHECK(t == resume);
  CHECK_FALSE(m.trusted());

  // The same clean stream earns trust again.
  for (int i = 1; i < 40; ++i) {
    const int64_t s = resume + sender_time(i);
    m.on_tick(comb_arrival(s), good_stamp(s));
  }
  CHECK(m.trusted());
}
