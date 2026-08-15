#pragma once

// The one-line-per-second UART telemetry the Studio Mode test plan's P4
// wants (docs/STUDIO_MODE_TEST_PLAN.md): a flat, host-parseable CSV line
// carrying the counters a run's pass/fail and phase-error analysis need,
// separate from the human-readable rate-limited ESP_LOG line that already
// exists for the same counters. Kept portable (no ESP-IDF, no FreeRTOS) so
// the line format is host-testable byte-for-byte; main/audio_service.cpp
// fills in a TelemetrySample from AudioStatus + config + heap + WiFi and
// writes the formatted line to the console UART.

#include <cstddef>
#include <cstdint>

namespace neon {

struct TelemetrySample {
  uint64_t uptime_ms = 0;
  // "sta" | "ap" | "apsta" | "eth" | "none". Never null.
  const char* mode = "none";
  uint8_t prio_set = 0;  // PriorityProfile: 0 = fixed, 1 = legacy
  uint16_t req_jitter_ms = 0;
  uint16_t eff_jitter_ms = 0;
  uint32_t jit_fill_frames = 0;
  uint32_t jit_underruns = 0;
  uint32_t jit_conceals = 0;
  uint8_t jit_state = 0;  // JitterBuffer::State: 0 idle, 1 buffering, 2 playing
  uint32_t rx_dropped = 0;
  uint32_t rx_high_water = 0;
  int32_t la_trim_ppm = 0;
  uint32_t i2s_write_failures = 0;
  int8_t rssi = 0;
  uint32_t heap_free_internal = 0;
  uint32_t heap_free_psram = 0;
};

// Column header matching telemetry_csv_line()'s field order, no trailing
// newline. Returns the length written, or 0 if `cap` is too small.
size_t telemetry_csv_header(char* out, size_t cap);

// One data line, no trailing newline (the caller appends "\n" — kept off
// so a caller writing straight to a UART driver controls line endings).
// Returns the length written, or 0 if `cap` is too small.
size_t telemetry_csv_line(const TelemetrySample& s, char* out, size_t cap);

}  // namespace neon
