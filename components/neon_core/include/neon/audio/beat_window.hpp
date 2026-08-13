#pragma once

// Snapshot → per-block beat window, in integer math only.
//
// The audio task knows when the block it is about to render will reach the
// converter (SampleClock + the DMA depth). These helpers turn that time
// range into the session beats it covers, so every source in the block can
// be placed by beat rather than by "roughly now". All beats are signed
// Q32.32, the same representation the timeline snapshot carries.

#include <cstdint>

#include "neon/fixed_math.hpp"
#include "neon/timeline.hpp"

namespace neon {

// Session beat at an absolute time, Q32.32. Returns 0 for an empty
// snapshot (tempo not yet known).
inline int64_t beat_at_us_q32(const TimelineSnapshot& tl, int64_t t_us) {
  if (tl.tempo_mpb_q32 == 0) {
    return 0;
  }
  const int64_t dt = t_us - tl.origin_us;
  const uint64_t mag = static_cast<uint64_t>(dt < 0 ? -dt : dt);
  // beats = dt / mpb_us, in Q32.32 = dt * 2^64 / mpb_q32. Done in two
  // 128/64 divisions so nothing overflows and nothing needs __int128.
  uint64_t rem = 0;
  const uint64_t hi =
      div_u128_u64_rem(U128{mag >> 32, mag << 32}, tl.tempo_mpb_q32, &rem);
  // A·2^32 = hi·M + rem, so A·2^64/M = hi·2^32 + (rem·2^32)/M.
  const uint64_t lo = div_u128_u64(U128{rem >> 32, rem << 32}, tl.tempo_mpb_q32);
  const int64_t beats = static_cast<int64_t>((hi << 32) + lo);
  return tl.beat_at_origin_q32 + (dt < 0 ? -beats : beats);
}

// Inverse: the absolute time at which the session reaches `beat_q32`.
inline int64_t us_at_beat_q32(const TimelineSnapshot& tl, int64_t beat_q32) {
  if (tl.tempo_mpb_q32 == 0) {
    return tl.origin_us;
  }
  const int64_t db = beat_q32 - tl.beat_at_origin_q32;
  const uint64_t mag = static_cast<uint64_t>(db < 0 ? -db : db);
  // µs = beats * mpb_us = (beats_q32 * mpb_q32) >> 64, i.e. the high word.
  const U128 p = mul_u64(mag, tl.tempo_mpb_q32);
  const int64_t us = static_cast<int64_t>(p.hi);
  return tl.origin_us + (db < 0 ? -us : us);
}

// The beats covered by one rendered block. `frames` is the block length;
// the map inside the block is linear, which is exactly what the snapshot
// already asserts about the tempo over a 3 ms window.
struct BeatWindow {
  int64_t begin_q32 = 0;
  int64_t end_q32 = 0;
  int64_t t0_us = 0;
  int64_t t1_us = 0;
  uint32_t frames = 0;
  uint32_t quantum_beats = 4;
  bool playing = false;
  bool valid = false;

  // Frame offset within the block at which the session reaches `beat_q32`.
  // Callers clamp/range-check; the result is only meaningful for beats
  // inside [begin_q32, end_q32).
  uint32_t frame_of_beat(int64_t beat_q32) const {
    const int64_t span = end_q32 - begin_q32;
    if (span <= 0 || frames == 0) {
      return 0;
    }
    const int64_t d = beat_q32 - begin_q32;
    if (d <= 0) {
      return 0;
    }
    const uint64_t f = div_u128_u64(mul_u64(static_cast<uint64_t>(d), frames),
                                    static_cast<uint64_t>(span));
    return f >= frames ? frames - 1 : static_cast<uint32_t>(f);
  }
};

inline BeatWindow beat_window(const TimelineSnapshot& tl, int64_t t0_us,
                              int64_t t1_us, uint32_t frames) {
  BeatWindow w;
  w.t0_us = t0_us;
  w.t1_us = t1_us;
  w.frames = frames;
  w.quantum_beats = tl.quantum_beats != 0 ? tl.quantum_beats : 4;
  w.playing = tl.playing != 0;
  if (tl.tempo_mpb_q32 == 0 || t1_us <= t0_us || frames == 0) {
    return w;
  }
  w.begin_q32 = beat_at_us_q32(tl, t0_us);
  w.end_q32 = beat_at_us_q32(tl, t1_us);
  w.valid = w.end_q32 > w.begin_q32;
  return w;
}

// Q32.32 helpers for whole beats. floor_beat is the largest whole beat at
// or below `beat_q32` (negative beats included — the session can run
// before its origin).
inline int64_t beat_q32_from_int(int64_t beats) {
  // Through unsigned: shifting a negative int64 is undefined before C++20,
  // and the beat grid runs before the session origin often enough.
  return static_cast<int64_t>(static_cast<uint64_t>(beats) << 32);
}

inline int64_t floor_beat(int64_t beat_q32) {
  return beat_q32 >> 32;  // arithmetic shift floors for negatives too
}

// Beat index (0-based) modulo the quantum, for downbeat accents.
inline uint32_t beat_in_bar(int64_t beat_index, uint32_t quantum) {
  const int64_t q = quantum != 0 ? static_cast<int64_t>(quantum) : 4;
  int64_t m = beat_index % q;
  if (m < 0) {
    m += q;
  }
  return static_cast<uint32_t>(m);
}

}  // namespace neon
