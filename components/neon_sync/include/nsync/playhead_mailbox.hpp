#pragma once

// Single-writer seqlock hand-off for DawPlayhead samples: the audio thread
// publishes the latest observation, the sync service thread reads it. The
// writer is wait-free (no locks, no allocation, no syscalls — safe on the
// audio callback); the reader retries on the rare torn read and keeps its
// previous sample. Every payload word is a relaxed atomic, so there is no
// data race in the C++ memory-model sense; the seq counter's odd/even
// protocol makes the words cohere.

#include <atomic>
#include <cstdint>
#include <cstring>

#include "nsync/daw_follower.hpp"

namespace nsync {

class PlayheadMailbox {
 public:
  // Audio-thread side. Latest write wins; no history.
  void publish(const DawPlayhead& s) noexcept {
    const uint32_t open = seq_.load(std::memory_order_relaxed) + 1;  // odd
    seq_.store(open, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    w_[0].store((s.valid ? 1u : 0u) | (s.playing ? 2u : 0u),
                std::memory_order_relaxed);
    w_[1].store(to_bits(s.bpm), std::memory_order_relaxed);
    w_[2].store(to_bits(s.beat), std::memory_order_relaxed);
    w_[3].store(static_cast<uint64_t>(s.sampled_us),
                std::memory_order_relaxed);
    seq_.store(open + 1, std::memory_order_release);
  }

  // Service-thread side. False before the first publish, or on the
  // (bounded, effectively-never) chance every retry collided with a write.
  bool read(DawPlayhead& out) const noexcept {
    for (int tries = 0; tries < 8; ++tries) {
      const uint32_t s1 = seq_.load(std::memory_order_acquire);
      if (s1 == 0) {
        return false;
      }
      if ((s1 & 1u) != 0) {
        continue;
      }
      const uint64_t flags = w_[0].load(std::memory_order_relaxed);
      const uint64_t bpm = w_[1].load(std::memory_order_relaxed);
      const uint64_t beat = w_[2].load(std::memory_order_relaxed);
      const uint64_t at = w_[3].load(std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (seq_.load(std::memory_order_relaxed) != s1) {
        continue;
      }
      out.valid = (flags & 1u) != 0;
      out.playing = (flags & 2u) != 0;
      out.bpm = from_bits(bpm);
      out.beat = from_bits(beat);
      out.sampled_us = static_cast<int64_t>(at);
      return true;
    }
    return false;
  }

 private:
  static uint64_t to_bits(double v) noexcept {
    uint64_t b = 0;
    std::memcpy(&b, &v, sizeof(b));
    return b;
  }
  static double from_bits(uint64_t b) noexcept {
    double v = 0.0;
    std::memcpy(&v, &b, sizeof(v));
    return v;
  }

  std::atomic<uint32_t> seq_{0};
  std::atomic<uint64_t> w_[4] = {{0}, {0}, {0}, {0}};
};

}  // namespace nsync
