#include <doctest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "neon/audio/frame_ring.hpp"

namespace {

constexpr uint32_t kCap = 256;

}  // namespace

TEST_CASE("FrameRing: capacity rounds down to a power of two") {
  std::vector<int16_t> storage(1000 * 2);
  neon::FrameRing ring;
  ring.init(storage.data(), 1000, 2);
  CHECK(ring.capacity() == 512);
  CHECK(ring.valid());
  CHECK(ring.available() == 0);
  CHECK(ring.space() == 512);
}

TEST_CASE("FrameRing: round trip preserves order across the wrap") {
  std::vector<int16_t> storage(kCap * 2);
  neon::FrameRing ring;
  ring.init(storage.data(), kCap, 2);

  int16_t next_in = 0;
  int16_t next_out = 0;
  std::vector<int16_t> chunk(64 * 2);
  std::vector<int16_t> got(64 * 2);
  // Ten full laps of the ring in uneven chunks.
  for (int round = 0; round < 40; ++round) {
    const uint32_t n = 17 + (round % 7) * 5;
    for (uint32_t i = 0; i < n; ++i) {
      chunk[2 * i] = next_in;
      chunk[2 * i + 1] = static_cast<int16_t>(-next_in);
      ++next_in;
    }
    CHECK(ring.write(chunk.data(), n) == n);
    CHECK(ring.read(got.data(), n) == n);
    for (uint32_t i = 0; i < n; ++i) {
      CHECK(got[2 * i] == next_out);
      CHECK(got[2 * i + 1] == static_cast<int16_t>(-next_out));
      ++next_out;
    }
  }
  CHECK(ring.dropped() == 0);
}

TEST_CASE("FrameRing: a full ring drops the newest by default") {
  std::vector<int16_t> storage(kCap * 2);
  neon::FrameRing ring;
  ring.init(storage.data(), kCap, 2);
  std::vector<int16_t> data(kCap * 2, 7);

  CHECK(ring.write(data.data(), kCap) == kCap);
  CHECK(ring.available() == kCap);
  CHECK(ring.write(data.data(), 10) == 0);
  CHECK(ring.dropped() == 0);  // backpressure, not loss: the caller retries
  CHECK(ring.available() == kCap);
}

TEST_CASE("FrameRing: drop_oldest keeps the newest audio") {
  std::vector<int16_t> storage(kCap * 2);
  neon::FrameRing ring;
  ring.init(storage.data(), kCap, 2);

  std::vector<int16_t> block(kCap * 2);
  for (uint32_t i = 0; i < kCap; ++i) {
    block[2 * i] = static_cast<int16_t>(i);
    block[2 * i + 1] = static_cast<int16_t>(i);
  }
  ring.write(block.data(), kCap);

  // Ten more frames, numbered 1000..1009.
  int16_t tail[20];
  for (int i = 0; i < 10; ++i) {
    tail[2 * i] = static_cast<int16_t>(1000 + i);
    tail[2 * i + 1] = static_cast<int16_t>(1000 + i);
  }
  CHECK(ring.write(tail, 10, /*drop_oldest=*/true) == 10);
  CHECK(ring.dropped() == 10);
  CHECK(ring.available() == kCap);

  std::vector<int16_t> got(kCap * 2);
  CHECK(ring.read(got.data(), kCap) == kCap);
  CHECK(got[0] == 10);                    // the first ten frames are gone
  CHECK(got[2 * (kCap - 1)] == 1009);     // the newest survived
}

TEST_CASE("FrameRing: a write larger than the ring keeps only the tail") {
  std::vector<int16_t> storage(kCap * 2);
  neon::FrameRing ring;
  ring.init(storage.data(), kCap, 2);
  std::vector<int16_t> huge(kCap * 4);
  for (uint32_t i = 0; i < kCap * 2; ++i) {
    huge[2 * i] = static_cast<int16_t>(i);
    huge[2 * i + 1] = 0;
  }
  CHECK(ring.write(huge.data(), kCap * 2, /*drop_oldest=*/true) == kCap);
  std::vector<int16_t> got(kCap * 2);
  ring.read(got.data(), kCap);
  CHECK(got[0] == static_cast<int16_t>(kCap));
}

TEST_CASE("FrameRing: a short read counts as an underrun") {
  std::vector<int16_t> storage(kCap * 2);
  neon::FrameRing ring;
  ring.init(storage.data(), kCap, 2);
  int16_t data[8] = {};
  ring.write(data, 4);
  int16_t out[16] = {};
  CHECK(ring.read(out, 8) == 4);
  CHECK(ring.underruns() == 1);
  CHECK(ring.read(out, 8) == 0);
  CHECK(ring.underruns() == 2);
}

TEST_CASE("FrameRing: discard drops from the front") {
  std::vector<int16_t> storage(kCap * 2);
  neon::FrameRing ring;
  ring.init(storage.data(), kCap, 2);
  std::vector<int16_t> block(kCap * 2);
  for (uint32_t i = 0; i < kCap; ++i) {
    block[2 * i] = static_cast<int16_t>(i);
    block[2 * i + 1] = static_cast<int16_t>(i);
  }
  ring.write(block.data(), 100);
  CHECK(ring.discard(40) == 40);
  CHECK(ring.available() == 60);
  int16_t out[2] = {};
  ring.read(out, 1);
  CHECK(out[0] == 40);
  CHECK(ring.discard(1000) == 59);
}

TEST_CASE("FrameRing: single producer and consumer on two threads") {
  std::vector<int16_t> storage(kCap * 2);
  neon::FrameRing ring;
  ring.init(storage.data(), kCap, 2);

  constexpr uint32_t kTotal = 200000;
  std::atomic<bool> mismatch{false};

  std::thread producer([&] {
    uint32_t sent = 0;
    int16_t chunk[64 * 2];
    while (sent < kTotal) {
      const uint32_t want = 1 + (sent % 61);
      for (uint32_t i = 0; i < want; ++i) {
        chunk[2 * i] = static_cast<int16_t>((sent + i) & 0x7fff);
        chunk[2 * i + 1] = static_cast<int16_t>(~((sent + i) & 0x7fff));
      }
      const uint32_t n = ring.write(chunk, want);
      sent += n;
      if (n == 0) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&] {
    uint32_t got = 0;
    int16_t chunk[64 * 2];
    while (got < kTotal) {
      const uint32_t n = ring.read(chunk, 64);
      for (uint32_t i = 0; i < n; ++i) {
        const int16_t expect = static_cast<int16_t>((got + i) & 0x7fff);
        if (chunk[2 * i] != expect ||
            chunk[2 * i + 1] != static_cast<int16_t>(~expect)) {
          mismatch.store(true);
        }
      }
      got += n;
      if (n == 0) {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();
  CHECK_FALSE(mismatch.load());
  CHECK(ring.dropped() == 0);
}

TEST_CASE("AudioBlockRing: blocks keep their beat window and format") {
  std::vector<int16_t> samples(4 * 512 * 2);
  std::vector<neon::AudioBlockInfo> infos(4);
  neon::AudioBlockRing ring;
  ring.init(samples.data(), infos.data(), 4, 512, 2);
  REQUIRE(ring.valid());

  neon::AudioBlockInfo in;
  in.frames = 8;
  in.sample_rate = 48000;
  in.channels = 2;
  in.begin_beat_q32 = 1ll << 32;
  in.end_beat_q32 = 3ll << 32;
  int16_t data[16];
  for (int i = 0; i < 16; ++i) data[i] = static_cast<int16_t>(i);
  CHECK(ring.push(in, data));
  CHECK(ring.queued() == 1);

  neon::AudioBlockInfo out{};
  int16_t got[16] = {};
  CHECK(ring.pop(&out, got, 16));
  CHECK(out.frames == 8);
  CHECK(out.sample_rate == 48000);
  CHECK(out.begin_beat_q32 == (1ll << 32));
  CHECK(out.end_beat_q32 == (3ll << 32));
  for (int i = 0; i < 16; ++i) CHECK(got[i] == i);
  CHECK(ring.queued() == 0);
  CHECK_FALSE(ring.pop(&out, got, 16));
}

TEST_CASE("AudioBlockRing: a full ring sheds the oldest block") {
  std::vector<int16_t> samples(4 * 8 * 2);
  std::vector<neon::AudioBlockInfo> infos(4);
  neon::AudioBlockRing ring;
  ring.init(samples.data(), infos.data(), 4, 8, 2);

  for (int b = 0; b < 6; ++b) {
    neon::AudioBlockInfo in;
    in.frames = 8;
    in.sample_rate = 48000;
    in.channels = 2;
    in.begin_beat_q32 = static_cast<int64_t>(b) << 32;
    int16_t data[16];
    for (int i = 0; i < 16; ++i) data[i] = static_cast<int16_t>(b);
    ring.push(in, data);
  }
  CHECK(ring.dropped() == 3);

  neon::AudioBlockInfo out{};
  int16_t got[16] = {};
  REQUIRE(ring.pop(&out, got, 16));
  CHECK(out.begin_beat_q32 == (3ll << 32));  // blocks 0..2 were shed
  CHECK(got[0] == 3);
}

TEST_CASE("AudioBlockRing: drain empties the queue without rewinding") {
  std::vector<int16_t> samples(4 * 8 * 2);
  std::vector<neon::AudioBlockInfo> infos(4);
  neon::AudioBlockRing ring;
  ring.init(samples.data(), infos.data(), 4, 8, 2);

  neon::AudioBlockInfo in;
  in.frames = 8;
  in.sample_rate = 48000;
  in.channels = 2;
  int16_t data[16] = {};
  for (int b = 0; b < 3; ++b) {
    ring.push(in, data);
  }
  CHECK(ring.queued() == 3);
  ring.drain();
  CHECK(ring.queued() == 0);

  // The ring keeps working after a drain: cursors moved forward, in step.
  data[0] = 42;
  CHECK(ring.push(in, data));
  neon::AudioBlockInfo out{};
  int16_t got[16] = {};
  REQUIRE(ring.pop(&out, got, 16));
  CHECK(got[0] == 42);
}

TEST_CASE("AudioBlockRing: oversized blocks are refused, not truncated") {
  std::vector<int16_t> samples(4 * 8 * 2);
  std::vector<neon::AudioBlockInfo> infos(4);
  neon::AudioBlockRing ring;
  ring.init(samples.data(), infos.data(), 4, 8, 2);

  neon::AudioBlockInfo in;
  in.frames = 9;
  in.channels = 2;
  in.sample_rate = 48000;
  int16_t data[32] = {};
  CHECK_FALSE(ring.push(in, data));
  CHECK(ring.queued() == 0);
  CHECK(ring.dropped() == 1);
}
