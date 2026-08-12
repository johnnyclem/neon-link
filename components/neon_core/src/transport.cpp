#include "neon/transport.hpp"

#include "neon/fixed_math.hpp"

namespace neon {

namespace {

// |a| as unsigned, for the signed Q32.32 helpers below.
uint64_t abs_u64(int64_t v) {
  return v < 0 ? static_cast<uint64_t>(-v) : static_cast<uint64_t>(v);
}

}  // namespace

int64_t beat_at_q32(const TimelineSnapshot& tl, int64_t t_us) {
  if (tl.tempo_mpb_q32 == 0) {
    return tl.beat_at_origin_q32;
  }
  const int64_t dt = t_us - tl.origin_us;
  const uint64_t adt = abs_u64(dt);
  // beats = dt / mpb. Both sides scaled by 2^32 to land in Q32.32, so the
  // numerator is dt << 64 — hence the {adt, 0} limb pair.
  const uint64_t dbeat = div_u128_u64(U128{adt, 0}, tl.tempo_mpb_q32);
  return tl.beat_at_origin_q32 +
         (dt < 0 ? -static_cast<int64_t>(dbeat) : static_cast<int64_t>(dbeat));
}

int64_t time_at_beat_q32(const TimelineSnapshot& tl, int64_t beat_q32) {
  if (tl.tempo_mpb_q32 == 0) {
    return tl.origin_us;
  }
  const int64_t d = beat_q32 - tl.beat_at_origin_q32;
  const U128 p = mul_u64(abs_u64(d), tl.tempo_mpb_q32);
  const int64_t off_us = static_cast<int64_t>(p.hi);  // integer µs part
  return d < 0 ? tl.origin_us - off_us : tl.origin_us + off_us;
}

int64_t next_loop_boundary_us(const TimelineSnapshot& tl, int64_t t_us) {
  const int64_t quantum =
      tl.quantum_beats != 0 ? static_cast<int64_t>(tl.quantum_beats) : 4;
  const int64_t beat_q32 = beat_at_q32(tl, t_us);
  const int64_t q_q32 = quantum << 32;
  // floor-divide so negative session beats behave, then step one loop on.
  int64_t loops = beat_q32 / q_q32;
  if (beat_q32 < 0 && (beat_q32 % q_q32) != 0) {
    --loops;
  }
  const int64_t next = (loops + 1) * q_q32;
  return time_at_beat_q32(tl, next);
}

// --- Tap tempo -------------------------------------------------------

void TapTempo::reset() {
  have_last_ = false;
  count_ = 0;
  next_ = 0;
}

bool TapTempo::tap(int64_t t_us, uint32_t* milli_bpm) {
  if (!have_last_) {
    last_us_ = t_us;
    have_last_ = true;
    return false;
  }
  const int64_t dt = t_us - last_us_;
  last_us_ = t_us;

  // 60e6 µs / interval = BPM; keep only intervals inside the tempo range.
  const int64_t kMaxIntervalUs = 60000000ll * 1000 / kMinMilliBpm;
  const int64_t kMinIntervalUs = 60000000ll * 1000 / kMaxMilliBpm;
  if (dt > kTimeoutUs || dt > kMaxIntervalUs || dt < kMinIntervalUs) {
    count_ = 0;  // out of range: this tap becomes the new anchor
    next_ = 0;
    return false;
  }

  intervals_[next_] = dt;
  next_ = (next_ + 1) % kMaxIntervals;
  if (count_ < kMaxIntervals) {
    ++count_;
  }

  int64_t sum = 0;
  for (int i = 0; i < count_; ++i) {
    sum += intervals_[i];
  }
  const int64_t avg = sum / count_;
  if (avg <= 0) {
    return false;
  }
  if (milli_bpm != nullptr) {
    *milli_bpm = clamp_milli_bpm(60000000ll * 1000 / avg);
  }
  return true;
}

// --- Tempo edits -----------------------------------------------------

uint32_t clamp_milli_bpm(int64_t milli_bpm) {
  if (milli_bpm < static_cast<int64_t>(kMinMilliBpm)) {
    return kMinMilliBpm;
  }
  if (milli_bpm > static_cast<int64_t>(kMaxMilliBpm)) {
    return kMaxMilliBpm;
  }
  return static_cast<uint32_t>(milli_bpm);
}

uint32_t nudge_milli_bpm(uint32_t milli_bpm, int delta_bpm) {
  return clamp_milli_bpm(static_cast<int64_t>(milli_bpm) +
                         static_cast<int64_t>(delta_bpm) * 1000);
}

uint32_t double_milli_bpm(uint32_t milli_bpm) {
  return clamp_milli_bpm(static_cast<int64_t>(milli_bpm) * 2);
}

uint32_t halve_milli_bpm(uint32_t milli_bpm) {
  return clamp_milli_bpm(static_cast<int64_t>(milli_bpm) / 2);
}

// --- Quantized transport --------------------------------------------

void TransportLatch::request(const TimelineSnapshot& tl, int64_t now_us,
                             bool play, bool quantized) {
  play_ = play;
  armed_ = true;
  if (!quantized || tl.tempo_mpb_q32 == 0) {
    fire_at_us_ = now_us;
    return;
  }
  fire_at_us_ = next_loop_boundary_us(tl, now_us);
}

bool TransportLatch::poll(int64_t now_us, bool* play_out) {
  if (!armed_ || now_us < fire_at_us_) {
    return false;
  }
  armed_ = false;
  if (play_out != nullptr) {
    *play_out = play_;
  }
  return true;
}

// --- Resync ----------------------------------------------------------

int64_t resync_target_us(const TimelineSnapshot& tl, int64_t now_us,
                         ResyncMode mode) {
  return mode == ResyncMode::kNow ? now_us
                                  : next_loop_boundary_us(tl, now_us);
}

}  // namespace neon
