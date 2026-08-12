#include <doctest.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "neon/timeline.hpp"

namespace {

// Payload whose fields carry an invariant a torn read would break.
struct Correlated {
  uint64_t x = 0;
  uint64_t y = 1;  // must always equal 2*x + 1
  uint64_t z = 0;  // must always equal x ^ 0xabcdef
};

}  // namespace

TEST_CASE("beat_number is 1-based inside the bar") {
  CHECK(neon::beat_number(0, 4) == 1);
  CHECK(neon::beat_number(999, 4) == 1);
  CHECK(neon::beat_number(1000, 4) == 2);
  CHECK(neon::beat_number(2000, 4) == 3);
  CHECK(neon::beat_number(3000, 4) == 4);
  CHECK(neon::beat_number(3999, 4) == 4);
  CHECK(neon::beat_number(0, 0) == 1);  // quantum 0 → 4
}

TEST_CASE("SeqLock: single-threaded round trip and versioning") {
  neon::SeqLock<neon::TimelineSnapshot> lock;
  CHECK(lock.version() == 0);

  neon::TimelineSnapshot in;
  in.tempo_mpb_q32 = 500000ull << 32;
  in.origin_us = 123456789;
  in.beat_at_origin_q32 = -(5ll << 32);
  in.playing = 1;
  in.num_peers = 3;
  lock.publish(in);
  CHECK(lock.version() == 2);

  neon::TimelineSnapshot out;
  const uint32_t v = lock.read(out);
  CHECK(v == 2);
  CHECK(out.tempo_mpb_q32 == in.tempo_mpb_q32);
  CHECK(out.origin_us == in.origin_us);
  CHECK(out.beat_at_origin_q32 == in.beat_at_origin_q32);
  CHECK(out.playing == 1);
  CHECK(out.num_peers == 3);

  lock.publish(in);
  CHECK(lock.version() == 4);
}

TEST_CASE("SeqLock: concurrent writer never yields a torn read") {
  neon::SeqLock<Correlated> lock;
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> torn{0};
  std::atomic<uint64_t> reads{0};

  std::thread reader([&] {
    Correlated c;
    while (!stop.load(std::memory_order_relaxed)) {
      lock.read(c);
      if (c.y != 2 * c.x + 1 || c.z != (c.x ^ 0xabcdefull)) {
        torn.fetch_add(1);
      }
      reads.fetch_add(1, std::memory_order_relaxed);
    }
  });

  Correlated c;
  for (uint64_t i = 1; i <= 200000; ++i) {
    c.x = i;
    c.y = 2 * i + 1;
    c.z = i ^ 0xabcdefull;
    lock.publish(c);
  }
  stop.store(true);
  reader.join();

  CHECK(torn.load() == 0);
  CHECK(reads.load() > 0);
}
