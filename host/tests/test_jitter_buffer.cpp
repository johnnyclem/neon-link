#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "neon/audio/jitter_buffer.hpp"

namespace {

constexpr uint32_t kOutRate = 44100;
constexpr uint32_t kSenderRate = 48000;
constexpr uint32_t kSenderBlock = 512;
constexpr uint32_t kOutBlock = 128;
constexpr uint32_t kRingFrames = 16384;  // ~340 ms at 48 kHz

// Beats per sender frame at 120 BPM: 0.5 s per beat.
int64_t beats_per_frame_q32(uint32_t rate) {
  return static_cast<int64_t>((1.0 / 0.5 / rate) * 4294967296.0);
}

struct Sender {
  int64_t beat_q32 = 0;
  double phase = 0.0;
  std::vector<int16_t> data = std::vector<int16_t>(kSenderBlock * 2);

  neon::AudioBlockInfo next(uint32_t frames = kSenderBlock,
                            uint8_t channels = 2) {
    neon::AudioBlockInfo info;
    info.frames = frames;
    info.sample_rate = kSenderRate;
    info.channels = channels;
    info.begin_beat_q32 = beat_q32;
    beat_q32 += static_cast<int64_t>(frames) * beats_per_frame_q32(kSenderRate);
    info.end_beat_q32 = beat_q32;
    data.assign(static_cast<size_t>(frames) * channels, 0);
    for (uint32_t i = 0; i < frames; ++i) {
      const double v = std::sin(phase) * 0.5;
      phase += 2.0 * M_PI * 1000.0 / kSenderRate;
      for (uint8_t c = 0; c < channels; ++c) {
        data[i * channels + c] = static_cast<int16_t>(v * 32767.0);
      }
    }
    return info;
  }
};

}  // namespace

TEST_CASE("JitterBuffer: buffers before it plays") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(60);
  CHECK(jb.state() == neon::JitterBuffer::State::kIdle);

  std::vector<float> l(kOutBlock), r(kOutBlock);
  CHECK(jb.pull(kOutBlock, l.data(), r.data()) == 0);
  for (float s : l) CHECK(s == 0.0f);

  Sender tx;
  jb.push(tx.next(), tx.data.data());
  CHECK(jb.state() == neon::JitterBuffer::State::kBuffering);
  CHECK(jb.sender_rate() == kSenderRate);
  // 60 ms at 48 kHz is 2880 frames; one 512-frame block is not enough.
  CHECK(jb.target_frames() == 2880);
  CHECK(jb.pull(kOutBlock, l.data(), r.data()) == 0);
  CHECK(jb.state() == neon::JitterBuffer::State::kBuffering);

  for (int i = 0; i < 6; ++i) {
    jb.push(tx.next(), tx.data.data());
  }
  CHECK(jb.pull(kOutBlock, l.data(), r.data()) == kOutBlock);
  CHECK(jb.state() == neon::JitterBuffer::State::kPlaying);
}

TEST_CASE("JitterBuffer: steady stream holds the jitter target and plays on") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(60);

  Sender tx;
  std::vector<float> l(kOutBlock), r(kOutBlock);

  // Run ~10 s of audio, pushing sender blocks at the rate the consumer
  // drains them (48000 in : 44100 out).
  double owed = 0.0;
  uint32_t produced_total = 0;
  for (int block = 0; block < 3400; ++block) {
    owed += static_cast<double>(kOutBlock) * kSenderRate / kOutRate;
    while (owed >= kSenderBlock) {
      jb.push(tx.next(), tx.data.data());
      owed -= kSenderBlock;
    }
    produced_total += jb.pull(kOutBlock, l.data(), r.data());
  }

  CHECK(jb.state() == neon::JitterBuffer::State::kPlaying);
  CHECK(jb.underruns() <= 2);          // the initial fill only
  CHECK(produced_total > 3300 * kOutBlock);

  // The fill sits near the target: within a couple of sender blocks.
  const int32_t err = static_cast<int32_t>(jb.fill_frames()) -
                      static_cast<int32_t>(jb.target_frames());
  CHECK(err < 1200);
  CHECK(err > -1200);

  // Which is the same statement in beats: the frame being played lags the
  // newest received one by the jitter target.
  const double lag_beats =
      static_cast<double>(jb.newest_beat_q32() - jb.read_beat_q32()) /
      4294967296.0;
  const double target_beats =
      static_cast<double>(jb.target_frames()) / kSenderRate / 0.5;
  CHECK(lag_beats == doctest::Approx(target_beats).epsilon(0.5));
  CHECK(lag_beats > 0.0);
}

TEST_CASE("JitterBuffer: the servo trims toward the target") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(60);

  Sender tx;
  std::vector<float> l(kOutBlock), r(kOutBlock);
  // A sender that keeps the buffer a couple of thousand frames past its
  // target: too full, persistently.
  const uint32_t high = jb.target_frames() + 2000;
  for (int i = 0; i < 300; ++i) {
    while (jb.fill_frames() < high) {
      jb.push(tx.next(), tx.data.data());
    }
    jb.pull(kOutBlock, l.data(), r.data());
  }
  // Too full -> consume input faster -> positive trim.
  CHECK(jb.trim_ppm() > 0);
  CHECK(jb.trim_ppm() <= neon::LinearResampler::kMaxTrimPpm);

  // And the other way: let it run dry-ish and the trim goes negative.
  for (int i = 0; i < 300; ++i) {
    jb.pull(kOutBlock, l.data(), r.data());
  }
  CHECK(jb.trim_ppm() < 0);
}

TEST_CASE("JitterBuffer: underrun falls back to buffering and recovers") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(20);

  Sender tx;
  std::vector<float> l(kOutBlock), r(kOutBlock);
  for (int i = 0; i < 4; ++i) {
    jb.push(tx.next(), tx.data.data());
  }
  while (jb.state() != neon::JitterBuffer::State::kPlaying) {
    jb.pull(kOutBlock, l.data(), r.data());
  }

  // The sender goes away.
  uint32_t drained = 0;
  while (jb.state() == neon::JitterBuffer::State::kPlaying && drained < 100) {
    jb.pull(kOutBlock, l.data(), r.data());
    ++drained;
  }
  CHECK(jb.state() == neon::JitterBuffer::State::kBuffering);
  CHECK(jb.underruns() >= 1);

  // Starved pulls are silence, not the last block repeated.
  jb.pull(kOutBlock, l.data(), r.data());
  for (float s : l) CHECK(s == 0.0f);

  // It comes back.
  for (int i = 0; i < 8; ++i) {
    jb.push(tx.next(), tx.data.data());
  }
  CHECK(jb.pull(kOutBlock, l.data(), r.data()) == kOutBlock);
  CHECK(jb.state() == neon::JitterBuffer::State::kPlaying);
}

TEST_CASE("JitterBuffer: an overrun sheds the oldest audio") {
  std::vector<int16_t> storage(2048 * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), 2048, kOutRate);
  jb.configure(20);

  Sender tx;
  for (int i = 0; i < 20; ++i) {  // 10240 frames into a 2048-frame ring
    jb.push(tx.next(), tx.data.data());
  }
  CHECK(jb.dropped() > 0);
  CHECK(jb.fill_frames() <= 2048);
}

TEST_CASE("JitterBuffer: mono senders are widened to stereo") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(10);

  Sender tx;
  for (int i = 0; i < 8; ++i) {
    jb.push(tx.next(kSenderBlock, /*channels=*/1), tx.data.data());
  }
  std::vector<float> l(kOutBlock), r(kOutBlock);
  while (jb.state() != neon::JitterBuffer::State::kPlaying) {
    if (jb.pull(kOutBlock, l.data(), r.data()) == 0 &&
        jb.state() == neon::JitterBuffer::State::kBuffering) {
      jb.push(tx.next(kSenderBlock, 1), tx.data.data());
    }
  }
  jb.pull(kOutBlock, l.data(), r.data());
  bool any = false;
  for (uint32_t i = 0; i < kOutBlock; ++i) {
    CHECK(l[i] == doctest::Approx(r[i]));
    if (l[i] != 0.0f) any = true;
  }
  CHECK(any);
}

TEST_CASE("JitterBuffer: a beat-time hole is concealed, not spliced") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(10);

  Sender tx;
  jb.push(tx.next(), tx.data.data());
  CHECK(jb.concealed() == 0);
  const uint32_t after_one = jb.fill_frames();

  (void)tx.next();  // this block never arrives
  jb.push(tx.next(), tx.data.data());
  CHECK(jb.concealed() == 1);
  // The missing 512 frames (or the 20 ms cap) were written as a fade-to-zero
  // so the two real blocks are not concatenated.
  CHECK(jb.fill_frames() >= after_one + kSenderBlock + kSenderBlock);
}

TEST_CASE("JitterBuffer: the jitter setting is clamped to something sane") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(0);
  CHECK(jb.jitter_ms() == neon::JitterBuffer::kMinJitterMs);
  jb.configure(100000);
  CHECK(jb.jitter_ms() == neon::JitterBuffer::kMaxJitterMs);
  // And the target never exceeds half the ring, whatever the user asks.
  CHECK(jb.target_frames() <= kRingFrames / 2);
}
