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

// A snapshotted sender block, for delivering out of order or twice.
struct Block {
  neon::AudioBlockInfo info;
  std::vector<int16_t> data;
};

Block take(Sender& tx, uint32_t frames = kSenderBlock, uint8_t channels = 2) {
  Block b;
  b.info = tx.next(frames, channels);
  b.data = tx.data;
  return b;
}

// Longest run of near-silent samples — a hole that was never repaired
// shows up as hundreds of these in a row.
uint32_t longest_quiet_run(const std::vector<float>& s) {
  uint32_t run = 0;
  uint32_t best = 0;
  for (float v : s) {
    run = (v > -1e-4f && v < 1e-4f) ? run + 1 : 0;
    if (run > best) best = run;
  }
  return best;
}

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

TEST_CASE("JitterBuffer: a mid-stream underrun resumes on a partial refill, "
          "not the full target") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(60);  // target_frames() == 2880 @ 48 kHz

  Sender tx;
  std::vector<float> l(kOutBlock), r(kOutBlock);
  for (int i = 0; i < 6; ++i) {
    jb.push(tx.next(), tx.data.data());
  }
  while (jb.state() != neon::JitterBuffer::State::kPlaying) {
    jb.pull(kOutBlock, l.data(), r.data());
  }

  // Drain it dry: a mid-stream underrun.
  uint32_t drained = 0;
  while (jb.state() == neon::JitterBuffer::State::kPlaying && drained < 100) {
    jb.pull(kOutBlock, l.data(), r.data());
    ++drained;
  }
  CHECK(jb.state() == neon::JitterBuffer::State::kBuffering);

  // Two 512-frame blocks (1024 frames) clear the ~720-frame quarter of the
  // 2880-frame target but are nowhere near the full target — the old "wait
  // for the full target again" rule would still be buffering here.
  jb.push(tx.next(), tx.data.data());
  jb.push(tx.next(), tx.data.data());
  CHECK(jb.fill_frames() < jb.target_frames());
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
  // The missing 512 frames (or the 100 ms cap) were written as a fade-to-zero
  // at their timeline position, so the two real blocks are not concatenated.
  CHECK(jb.fill_frames() >= after_one + kSenderBlock + kSenderBlock);
}

TEST_CASE("JitterBuffer: a reordered packet lands in the hole it belongs in") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  // Output at the sender's rate so the pulled audio is the pushed sine.
  jb.init(storage.data(), kRingFrames, kSenderRate);
  jb.configure(10);

  Sender tx;
  Block a = take(tx);
  Block b = take(tx);
  Block c = take(tx);

  jb.push(a.info, a.data.data());
  jb.push(c.info, c.data.data());  // arrives early: a hole where b belongs
  CHECK(jb.concealed() == 1);
  CHECK(jb.fill_frames() == 3 * kSenderBlock);

  jb.push(b.info, b.data.data());  // late, but its slot is still queued
  CHECK(jb.fill_frames() == 3 * kSenderBlock);  // placed, not appended
  CHECK(jb.dropped() == 0);

  // Play the three blocks out: the hole was filled with the real audio,
  // so there is no silent stretch anywhere.
  std::vector<float> out;
  std::vector<float> l(kOutBlock), r(kOutBlock);
  for (int i = 0; i < 11; ++i) {
    CHECK(jb.pull(kOutBlock, l.data(), r.data()) == kOutBlock);
    out.insert(out.end(), l.begin(), l.end());
  }
  CHECK(longest_quiet_run(out) < 32);
}

TEST_CASE("JitterBuffer: a block stamped at beat zero is placed, not "
          "mistaken for an unstamped one") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kSenderRate);
  jb.configure(10);

  // A sender counting in: the block before the downbeat spans [-512, 0)
  // in frames, so the next one begins at exactly beat 0.
  Sender tx;
  tx.beat_q32 = -512 * beats_per_frame_q32(kSenderRate);
  Block a = take(tx);
  Block b = take(tx);  // begin_beat_q32 == 0
  CHECK(b.info.begin_beat_q32 == 0);
  Block c = take(tx);

  jb.push(a.info, a.data.data());
  jb.push(c.info, c.data.data());  // hole where b belongs
  CHECK(jb.concealed() == 1);
  CHECK(jb.fill_frames() == 3 * kSenderBlock);

  // The late block's beat-0 stamp must still count as a stamp: it drops
  // into its hole instead of being appended at the end.
  jb.push(b.info, b.data.data());
  CHECK(jb.fill_frames() == 3 * kSenderBlock);
  CHECK(jb.dropped() == 0);
}

TEST_CASE("JitterBuffer: an unstamped block appends and leaves the beat "
          "anchor alone") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(10);

  Sender tx;
  for (int i = 0; i < 2; ++i) {
    jb.push(tx.next(), tx.data.data());
  }
  const int64_t anchor = jb.newest_beat_q32();
  const uint32_t fill = jb.fill_frames();

  neon::AudioBlockInfo info;  // begin/end default to kInvalidBeatQ32
  info.frames = kSenderBlock;
  info.sample_rate = kSenderRate;
  info.channels = 2;
  std::vector<int16_t> silence(kSenderBlock * 2, 0);
  jb.push(info, silence.data());
  CHECK(jb.fill_frames() == fill + kSenderBlock);
  CHECK(jb.newest_beat_q32() == anchor);
  CHECK(jb.concealed() == 0);
}

TEST_CASE("JitterBuffer: a duplicate block is an idempotent overwrite") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(10);

  Sender tx;
  Block a = take(tx);
  Block b = take(tx);
  jb.push(a.info, a.data.data());
  jb.push(b.info, b.data.data());
  const uint32_t fill = jb.fill_frames();

  jb.push(b.info, b.data.data());  // the network delivered it twice
  CHECK(jb.fill_frames() == fill);
  CHECK(jb.concealed() == 0);
  CHECK(jb.dropped() == 0);
}

TEST_CASE("JitterBuffer: a block later than its playback deadline is dropped") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kSenderRate);
  jb.configure(10);

  Sender tx;
  Block b1 = take(tx);
  Block b2 = take(tx);
  Block b3 = take(tx);
  Block b4 = take(tx);
  jb.push(b1.info, b1.data.data());
  jb.push(b2.info, b2.data.data());
  jb.push(b3.info, b3.data.data());
  jb.push(b4.info, b4.data.data());

  // Play well past b2's region.
  std::vector<float> l(kOutBlock), r(kOutBlock);
  for (int i = 0; i < 12; ++i) {
    jb.pull(kOutBlock, l.data(), r.data());
  }

  const uint32_t fill = jb.fill_frames();
  jb.push(b2.info, b2.data.data());  // whole block already played out
  CHECK(jb.dropped() == kSenderBlock);
  CHECK(jb.fill_frames() == fill);
}

TEST_CASE("JitterBuffer: a partially late block is clipped, not discarded") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kSenderRate);
  jb.configure(60);

  Sender tx;
  std::vector<Block> blocks;
  for (int i = 0; i < 6; ++i) {
    blocks.push_back(take(tx));
    jb.push(blocks.back().info, blocks.back().data.data());
  }

  // Consume so the read cursor sits inside block 3's region.
  std::vector<float> l(kOutBlock), r(kOutBlock);
  for (int i = 0; i < 6; ++i) {
    jb.pull(kOutBlock, l.data(), r.data());
  }

  const uint32_t fill = jb.fill_frames();
  jb.push(blocks[2].info, blocks[2].data.data());
  // Its head was already played (dropped); its tail still had a slot.
  CHECK(jb.dropped() > 0);
  CHECK(jb.dropped() < kSenderBlock);
  CHECK(jb.fill_frames() == fill);
}

TEST_CASE("JitterBuffer: a backward timeline jump appends, audio being "
          "continuous across a loop wrap") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(10);

  Sender tx;
  for (int i = 0; i < 6; ++i) {
    jb.push(tx.next(), tx.data.data());
  }
  const uint32_t fill = jb.fill_frames();

  // The sender loops back near the top of the bar; its audio stream does
  // not pause. Beats restart far behind the newest received beat.
  Sender wrapped;
  wrapped.beat_q32 = 20 * beats_per_frame_q32(kSenderRate);
  jb.push(wrapped.next(), wrapped.data.data());
  CHECK(jb.fill_frames() == fill + kSenderBlock);
  CHECK(jb.concealed() == 0);
  CHECK(jb.dropped() == 0);
  // The beat anchor followed the jump.
  CHECK(jb.newest_beat_q32() == wrapped.beat_q32);

  jb.push(wrapped.next(), wrapped.data.data());  // and the stream carries on
  CHECK(jb.fill_frames() == fill + 2 * kSenderBlock);
  CHECK(jb.concealed() == 0);
}

TEST_CASE("JitterBuffer: a forward jump past the conceal cap re-anchors") {
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kOutRate);
  jb.configure(10);

  Sender tx;
  jb.push(tx.next(), tx.data.data());

  for (int i = 0; i < 100; ++i) {
    (void)tx.next();  // ~1 s of beat time the receiver never sees
  }
  jb.push(tx.next(), tx.data.data());
  CHECK(jb.concealed() == 1);
  // The gap was capped, not buffered in full ...
  CHECK(jb.fill_frames() ==
        kSenderBlock + neon::JitterBuffer::kMaxConcealFrames + kSenderBlock);

  // ... and the stream is re-anchored: the next in-order block extends it
  // with no further concealment.
  jb.push(tx.next(), tx.data.data());
  CHECK(jb.concealed() == 1);
  CHECK(jb.fill_frames() ==
        kSenderBlock + neon::JitterBuffer::kMaxConcealFrames +
        2 * kSenderBlock);
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

// docs/STUDIO_MODE_TEST_PLAN.md P3/§C5: the ring-capacity clamp used to be
// silent (the requested jitter_ms and the actually-applied target could
// diverge with nothing to say so). effective_jitter_ms() exists so a test
// run can tell the two apart instead of discovering the clamp by surprise.
TEST_CASE("JitterBuffer: effective jitter reports the capacity clamp") {
  // 48000, not kOutRate (44100): a rate that divides 1000 evenly keeps the
  // ms<->frames round trip exact, so the "no clamp" case below asserts
  // effective == requested rather than tripping over configure()'s own
  // (rate/1000)*ms truncation — a separate, sub-millisecond rounding
  // artifact this test is not about.
  constexpr uint32_t kRate = 48000;
  std::vector<int16_t> storage(kRingFrames * 2);
  neon::JitterBuffer jb;
  jb.init(storage.data(), kRingFrames, kRate);

  // A request well inside half the ring is honored exactly: requested and
  // effective agree.
  jb.configure(60);
  CHECK(jb.jitter_ms() == 60);
  CHECK(jb.effective_jitter_ms() == 60);

  // A request past half the ring's capacity (in ms, at the output rate) is
  // silently truncated at the frame level; effective_jitter_ms() must say
  // so even though jitter_ms() still reports what was asked for.
  const uint32_t half_ring_ms = (kRingFrames / 2) * 1000u / kRate;
  jb.configure(half_ring_ms + 100);
  CHECK(jb.jitter_ms() == half_ring_ms + 100);
  CHECK(jb.effective_jitter_ms() < jb.jitter_ms());
  CHECK(jb.target_frames() <= kRingFrames / 2);
}
