#pragma once

// 1 Hz CSV telemetry for audio-in tempo follow. Same portable formatter
// idiom as midi_pll_csv.hpp; the service layer prints "AFOL,<line>" on
// the console UART (tools/studio_mode/uart_telemetry_logger.py --prefix
// AFOL captures it).

#include <cstddef>
#include <cstdint>

#include "neon/audio/types.hpp"

namespace neon {

struct AudioFollowTelemetrySample {
  int64_t t_us = 0;
  uint8_t lock = 0;           // 0 idle 1 acquiring 2 locked
  uint8_t subdiv = 0;
  uint16_t onset_hz_x10 = 0;
  uint32_t est_mbpm = 0;
  uint32_t pub_mbpm = 0;
  uint32_t onsets = 0;
  uint32_t rejects = 0;
  uint8_t following = 0;
};

AudioFollowTelemetrySample audio_follow_telemetry_sample(
    const FollowStatus& st, int64_t t_us, bool following);

// Column header matching audio_follow_telemetry_csv_line()'s field order,
// no trailing newline. Returns the length written, or 0 if `cap` is too small.
size_t audio_follow_telemetry_csv_header(char* out, size_t cap);

// One data line, no trailing newline. Returns 0 if `cap` is too small.
size_t audio_follow_telemetry_csv_line(const AudioFollowTelemetrySample& s,
                                       char* out, size_t cap);

}  // namespace neon
