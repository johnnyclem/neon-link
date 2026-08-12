#pragma once

// The inter-core contract: core 0 (networking / Ableton Link) publishes a
// TimelineSnapshot whenever the session state materially changes; core 1
// (pulse engine) reads it lock-free at each scheduling refill and projects
// upcoming edges from it. The engine never calls Link in the hot path.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace neon {

struct TimelineSnapshot {
  uint64_t tempo_mpb_q32 = 0;      // microseconds per beat, Q32.32
  int64_t origin_us = 0;           // capture time in the shared µs timebase
  int64_t beat_at_origin_q32 = 0;  // session beat at origin_us, Q32.32 signed
  uint32_t quantum_beats = 4;
  uint8_t playing = 0;
  uint8_t pad_[3] = {0, 0, 0};
  uint32_t num_peers = 0;
  int32_t latency_us = 0;  // reserved until milestone 3
};

// Position within the current bar, in milli-beats (0..quantum*1000).
//
// Both UIs derive the phase from this one function so the panel's bar and
// the web strip's mirror of it cannot disagree about where the bar is —
// which would be the most obvious possible way for the two surfaces to stop
// feeling like one instrument.
inline uint32_t phase_milli_beats(const TimelineSnapshot& tl, int64_t now_us) {
  if (tl.tempo_mpb_q32 == 0) {
    return 0;
  }
  const double mpb_us = static_cast<double>(tl.tempo_mpb_q32) / 4294967296.0;
  const double beat = static_cast<double>(tl.beat_at_origin_q32) / 4294967296.0 +
                      static_cast<double>(now_us - tl.origin_us) / mpb_us;
  const double q =
      static_cast<double>(tl.quantum_beats != 0 ? tl.quantum_beats : 4);
  double bar_pos = beat - static_cast<double>(static_cast<int64_t>(beat / q)) * q;
  if (bar_pos < 0) {
    bar_pos += q;
  }
  return static_cast<uint32_t>(bar_pos * 1000.0);
}

// Single-writer / single-reader seqlock. The payload is stored as relaxed
// atomic words (data-race-free by construction) and the protocol is fenced
// with seq_cst barriers on both sides — conservative and cheap at the call
// rates involved (writer: on session change; reader: every ~5 ms refill).
//
// Versions are even when stable; a reader retries while a write is in
// flight. read() returns the version so consumers can cheaply detect "no
// change since last time" via version().
template <typename T>
class SeqLock {
  static_assert(std::is_trivially_copyable<T>::value,
                "SeqLock payload must be trivially copyable");

 public:
  void publish(const T& value) {
    uint32_t words[kWords];
    std::memcpy(words, &value, sizeof(T));
    const uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed);  // odd: write in flight
    std::atomic_thread_fence(std::memory_order_seq_cst);
    for (size_t i = 0; i < kWords; ++i) {
      words_[i].store(words[i], std::memory_order_relaxed);
    }
    std::atomic_thread_fence(std::memory_order_seq_cst);
    seq_.store(s + 2, std::memory_order_relaxed);
  }

  // Blocks (spins) only while a write is in flight; returns the version.
  uint32_t read(T& out) const {
    uint32_t words[kWords];
    for (;;) {
      const uint32_t s1 = seq_.load(std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_seq_cst);
      if (s1 & 1u) {
        continue;
      }
      for (size_t i = 0; i < kWords; ++i) {
        words[i] = words_[i].load(std::memory_order_relaxed);
      }
      std::atomic_thread_fence(std::memory_order_seq_cst);
      const uint32_t s2 = seq_.load(std::memory_order_relaxed);
      if (s1 == s2) {
        std::memcpy(&out, words, sizeof(T));
        return s1;
      }
    }
  }

  uint32_t version() const { return seq_.load(std::memory_order_acquire); }

 private:
  static constexpr size_t kWords = (sizeof(T) + 3) / 4;

  std::atomic<uint32_t> seq_{0};
  std::atomic<uint32_t> words_[kWords] = {};
};

}  // namespace neon
