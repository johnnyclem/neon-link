#pragma once

// Dongle telemetry (XIAO / e-paper link-sync). Same TEL, prefix as the
// Studio Mode P4 stream so uart_telemetry_logger.py can capture it.
// Audio fields are omitted — this target has no audio engine.

#include <cstddef>
#include <cstdint>

namespace neon {

struct LinkSyncTelemetrySample {
  uint64_t uptime_ms = 0;
  const char* mode = "none";  // "sta" | "ap" | "apsta" | "none"
  int8_t rssi = 0;
  uint32_t heap_free_internal = 0;
  uint32_t heap_free_psram = 0;
  uint32_t peers = 0;
  uint8_t playing = 0;
  uint32_t milli_bpm = 0;
  uint32_t pulse_edges = 0;
  uint32_t late_max_us = 0;
  uint32_t late_avg_us = 0;
};

size_t linksync_telemetry_csv_header(char* out, size_t cap);
size_t linksync_telemetry_csv_line(const LinkSyncTelemetrySample& s, char* out,
                                   size_t cap);

}  // namespace neon
