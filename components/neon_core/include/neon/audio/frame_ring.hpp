#pragma once

// Lock-free single-producer / single-consumer rings for audio.
//
// These are the only things that cross between the audio task on core 1
// and the network side on core 0. Storage is supplied by the owner so the
// firmware can put the large rings in PSRAM and keep DMA buffers internal;
// the host tests hand them plain arrays.
//
// FrameRing carries a flat stream of interleaved frames. AudioBlockRing
// carries whole received blocks with the beat window and format they
// arrived with, which is what the Link Audio receive path needs.

#include <atomic>
#include <cstdint>
#include <cstring>

namespace neon {

// Ring positions are free-running uint32 counters, so the index mapping
// has to survive the 2^32 wrap (27 hours at 44.1 kHz — a module left
// running over a weekend reaches it). Powers of two are the only sizes
// where that mapping stays consistent, so capacities are rounded down.
inline uint32_t floor_pow2(uint32_t v) {
  if (v == 0) {
    return 0;
  }
  uint32_t p = 1;
  while (p <= v / 2) {
    p <<= 1;
  }
  return p;
}

class FrameRing {
 public:
  // `capacity_frames` is rounded down to a power of two and must end up
  // >= 2. Storage holds capacity*channels int16 samples.
  void init(int16_t* storage, uint32_t capacity_frames, uint8_t channels) {
    buf_ = storage;
    capacity_ = floor_pow2(capacity_frames);
    mask_ = capacity_ != 0 ? capacity_ - 1 : 0;
    channels_ = channels != 0 ? channels : 1;
    reset();
  }

  void reset() {
    write_.store(0, std::memory_order_relaxed);
    read_.store(0, std::memory_order_relaxed);
    dropped_.store(0, std::memory_order_relaxed);
    underruns_.store(0, std::memory_order_relaxed);
  }

  bool valid() const { return buf_ != nullptr && capacity_ >= 2; }
  uint32_t capacity() const { return capacity_; }
  uint8_t channels() const { return channels_; }

  uint32_t available() const {
    const uint32_t w = write_.load(std::memory_order_acquire);
    const uint32_t r = read_.load(std::memory_order_acquire);
    const uint32_t used = w - r;  // unsigned wraparound is intentional
    return used > capacity_ ? 0 : used;
  }

  uint32_t space() const { return capacity_ - available(); }

  // Producer side. Writes as many frames as fit and returns that count —
  // a short write is backpressure, which the caller is expected to see in
  // the return value rather than in the drop counter. With `drop_oldest`,
  // the ring makes room by discarding the oldest frames instead, which is
  // what a monitor path wants: the newest audio, always.
  uint32_t write(const int16_t* interleaved, uint32_t frames,
                 bool drop_oldest = false) {
    if (!valid() || interleaved == nullptr || frames == 0) {
      return 0;
    }
    if (frames > capacity_) {
      // Never meaningful to keep more than the ring holds.
      const uint32_t skip = frames - capacity_;
      interleaved += static_cast<size_t>(skip) * channels_;
      dropped_.fetch_add(skip, std::memory_order_relaxed);
      frames = capacity_;
    }
    uint32_t room = space();
    if (frames > room) {
      if (!drop_oldest) {
        frames = room;
      } else {
        // Advance the read cursor from the writer. Both sides only ever
        // move it forward, and available()/read() clamp, so the worst a
        // concurrent read can cause is one over-drop — counted, not
        // corrupting.
        const uint32_t need = frames - room;
        read_.fetch_add(need, std::memory_order_acq_rel);
        dropped_.fetch_add(need, std::memory_order_relaxed);
      }
    }
    if (frames == 0) {
      return 0;
    }
    const uint32_t w = write_.load(std::memory_order_relaxed) & mask_;
    const uint32_t first = frames < capacity_ - w ? frames : capacity_ - w;
    std::memcpy(buf_ + static_cast<size_t>(w) * channels_, interleaved,
                static_cast<size_t>(first) * channels_ * sizeof(int16_t));
    if (frames > first) {
      std::memcpy(buf_,
                  interleaved + static_cast<size_t>(first) * channels_,
                  static_cast<size_t>(frames - first) * channels_ *
                      sizeof(int16_t));
    }
    write_.fetch_add(frames, std::memory_order_release);
    return frames;
  }

  // Consumer side. Returns the number of frames actually read; a short
  // read counts as an underrun.
  uint32_t read(int16_t* out, uint32_t frames) {
    if (!valid() || out == nullptr || frames == 0) {
      return 0;
    }
    const uint32_t have = available();
    const uint32_t n = frames < have ? frames : have;
    if (n < frames) {
      underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    if (n == 0) {
      return 0;
    }
    const uint32_t r = read_.load(std::memory_order_relaxed) & mask_;
    const uint32_t first = n < capacity_ - r ? n : capacity_ - r;
    std::memcpy(out, buf_ + static_cast<size_t>(r) * channels_,
                static_cast<size_t>(first) * channels_ * sizeof(int16_t));
    if (n > first) {
      std::memcpy(out + static_cast<size_t>(first) * channels_, buf_,
                  static_cast<size_t>(n - first) * channels_ *
                      sizeof(int16_t));
    }
    read_.fetch_add(n, std::memory_order_acq_rel);
    return n;
  }

  // Consumer side: throw away the oldest `frames` without copying them.
  uint32_t discard(uint32_t frames) {
    const uint32_t have = available();
    const uint32_t n = frames < have ? frames : have;
    if (n != 0) {
      read_.fetch_add(n, std::memory_order_acq_rel);
      dropped_.fetch_add(n, std::memory_order_relaxed);
    }
    return n;
  }

  uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
  uint32_t underruns() const {
    return underruns_.load(std::memory_order_relaxed);
  }

 private:
  int16_t* buf_ = nullptr;
  uint32_t capacity_ = 0;
  uint32_t mask_ = 0;
  uint8_t channels_ = 1;
  std::atomic<uint32_t> write_{0};
  std::atomic<uint32_t> read_{0};
  std::atomic<uint32_t> dropped_{0};
  std::atomic<uint32_t> underruns_{0};
};

// One received (or outgoing) Link Audio block: the frames plus where they
// belong on the session timeline and what format they arrived in.
struct AudioBlockInfo {
  uint32_t frames = 0;
  uint32_t sample_rate = 0;
  uint8_t channels = 2;
  uint8_t pad_[3] = {};
  int64_t begin_beat_q32 = 0;
  int64_t end_beat_q32 = 0;
};

// SPSC ring of whole blocks. Sample storage is a flat region carved into
// `slots` fixed-size cells, so a block never straddles the wrap and the
// consumer can hand out a contiguous pointer.
class AudioBlockRing {
 public:
  void init(int16_t* samples, AudioBlockInfo* infos, uint32_t slots,
            uint32_t max_frames, uint8_t max_channels) {
    samples_ = samples;
    infos_ = infos;
    slots_ = floor_pow2(slots);
    slot_mask_ = slots_ != 0 ? slots_ - 1 : 0;
    max_frames_ = max_frames;
    max_channels_ = max_channels != 0 ? max_channels : 2;
    reset();
  }

  void reset() {
    write_.store(0, std::memory_order_relaxed);
    read_.store(0, std::memory_order_relaxed);
    dropped_.store(0, std::memory_order_relaxed);
  }

  bool valid() const {
    return samples_ != nullptr && infos_ != nullptr && slots_ >= 2;
  }

  uint32_t queued() const {
    const uint32_t used = write_.load(std::memory_order_acquire) -
                          read_.load(std::memory_order_acquire);
    return used > slots_ ? 0 : used;
  }

  // Producer: copies the block in. When the ring is full the oldest block
  // is discarded — a late block is worth less than a fresh one.
  bool push(const AudioBlockInfo& info, const int16_t* interleaved) {
    if (!valid() || interleaved == nullptr || info.frames == 0) {
      return false;
    }
    if (info.frames > max_frames_ || info.channels > max_channels_) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    if (queued() >= slots_ - 1) {
      read_.fetch_add(1, std::memory_order_acq_rel);
      dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    const uint32_t w = write_.load(std::memory_order_relaxed) & slot_mask_;
    infos_[w] = info;
    std::memcpy(slot(w), interleaved,
                static_cast<size_t>(info.frames) * info.channels *
                    sizeof(int16_t));
    write_.fetch_add(1, std::memory_order_release);
    return true;
  }

  // Consumer: copies the oldest block out. `cap_samples` guards `out`.
  bool pop(AudioBlockInfo* info, int16_t* out, uint32_t cap_samples) {
    if (!valid() || queued() == 0) {
      return false;
    }
    const uint32_t r = read_.load(std::memory_order_relaxed) & slot_mask_;
    const AudioBlockInfo in = infos_[r];
    const uint32_t samples = in.frames * in.channels;
    if (out != nullptr && samples <= cap_samples) {
      std::memcpy(out, slot(r), static_cast<size_t>(samples) * sizeof(int16_t));
    } else if (out != nullptr) {
      read_.fetch_add(1, std::memory_order_acq_rel);
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    if (info != nullptr) {
      *info = in;
    }
    read_.fetch_add(1, std::memory_order_acq_rel);
    return true;
  }

  uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
  uint32_t max_frames() const { return max_frames_; }
  uint8_t max_channels() const { return max_channels_; }

 private:
  int16_t* slot(uint32_t index) {
    return samples_ +
           static_cast<size_t>(index) * max_frames_ * max_channels_;
  }

  int16_t* samples_ = nullptr;
  AudioBlockInfo* infos_ = nullptr;
  uint32_t slots_ = 0;
  uint32_t slot_mask_ = 0;
  uint32_t max_frames_ = 0;
  uint8_t max_channels_ = 2;
  std::atomic<uint32_t> write_{0};
  std::atomic<uint32_t> read_{0};
  std::atomic<uint32_t> dropped_{0};
};

}  // namespace neon
