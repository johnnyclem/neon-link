#pragma once

// Maps BLE-MIDI 13-bit sender timestamps onto the local µs timebase
// (docs/SPIKE_MIDI_PLL.md §6.1.3, §8.5; phases handoff §B). Raw BLE
// arrivals are burst-quantized to the connection interval — the PLL's
// documented degraded mode. The in-packet stamps carry sender-side
// spacing to ±1 ms, but in the sender's clock, modulo 8.192 s; this is
// the miniature PeerClock that turns them into local times:
//
//   offset = arrival_us − sender_ms·1000
//
// filtered by a small median window (a TTL prunes stale samples so the
// tens-of-ppm drift between the two crystals cannot skew the estimate),
// with the 13-bit value unwrapped against the modulus using packet
// arrival as the coarse reference. The recovered time is
// sender_unwrapped·1000 + median(offset): the sender's own grid, placed
// at the *typical* delivery latency — a constant the loop never sees,
// while the per-packet jitter that poisons raw arrivals cancels out.
//
// Degenerate senders (spike §8.5): some stacks stamp at the
// connection-interval bucket, making the stamps a copy of the arrival
// comb. Detected from the decoded inter-tick spacing itself — a real
// sender's clock ticks are near-uniform (dispersion ≈ the 1 ms stamp
// granularity), a degenerate one's show the comb (dispersion ≈ the
// connection interval) — in which case the mapper never earns trust and
// callers stay in raw-arrival mode (Transport::kBle gains).
//
// Pure logic, host-tested (test_ble_time_mapper.cpp).

#include <cstdint>

namespace neon {
namespace midi {

class BleTimeMapper {
 public:
  // Feed one clock tick: its raw arrival time and decoded 13-bit sender
  // stamp (0..8191 ms). Returns the time the PLL should see — the mapped
  // sender time once the stamps have earned trust, the raw arrival time
  // while warming up and forever for a degenerate sender.
  int64_t on_tick(int64_t arrival_us, uint16_t sender_ms13);

  // The stamps are proven non-degenerate and the offset window is warm:
  // on_tick returns mapped times and the stream deserves the tighter
  // (kUsb) gain set.
  bool trusted() const { return trusted_; }

  void reset();

 private:
  // 13-bit millisecond clock: wraps every 8.192 s.
  static constexpr int32_t kModulusMs = 8192;
  // An arrival gap long enough that unwrapping against it is guesswork
  // (and the PLL will have reset anyway — its timeout is ≤ 1 s at
  // musical tempos): start over.
  static constexpr int64_t kResetGapUs = 2000000;
  // Offset samples: window and TTL. At musical tempos the window spans
  // well under the TTL; at the 20 BPM floor the TTL is what keeps only
  // drift-fresh samples in the median.
  static constexpr uint32_t kWindow = 16;
  static constexpr int64_t kSampleTtlUs = 2000000;
  // Spacing dispersion: mean |Δ − median(Δ)| · kDispersionNum >
  // median(Δ) reads as the comb. Real stamps sit near 5 % of the period
  // (1 ms granularity at 20 ms ticks); the comb sits near 50–100 %.
  static constexpr int32_t kDispersionNum = 4;
  // Consecutive clean dispersion verdicts (each over a full window of
  // deltas) before mapped times are trusted: with the window fill this
  // makes trust arrive after one beat of ticks, and a single comb-like
  // window instantly revokes it — decoded spacing is immune to link
  // jitter, so a dirty window means the sender itself, not the radio.
  static constexpr uint32_t kTrustEvals = 8;

  bool spacing_clean() const;

  bool have_last_ = false;
  int64_t last_arrival_us_ = 0;
  int32_t last_ms_ = 0;
  int64_t sender_ms_unwrapped_ = 0;

  int64_t offsets_[kWindow] = {};
  int64_t offset_at_us_[kWindow] = {};
  uint32_t offset_count_ = 0;
  uint32_t offset_next_ = 0;

  int32_t deltas_ms_[kWindow] = {};
  uint32_t delta_count_ = 0;
  uint32_t delta_next_ = 0;

  uint32_t clean_streak_ = 0;
  bool trusted_ = false;
};

}  // namespace midi
}  // namespace neon
