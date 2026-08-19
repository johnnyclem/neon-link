#include "neon/telemetry/linksync_csv.hpp"

#include <cstdio>

namespace neon {

size_t linksync_telemetry_csv_header(char* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const int n = std::snprintf(
      out, cap,
      "uptime_ms,mode,rssi,heap_free_internal,heap_free_psram,"
      "peers,playing,milli_bpm,pulse_edges,late_max_us,late_avg_us");
  return n > 0 && static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;
}

size_t linksync_telemetry_csv_line(const LinkSyncTelemetrySample& s, char* out,
                                   size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const int n = std::snprintf(
      out, cap, "%llu,%s,%d,%lu,%lu,%lu,%u,%lu,%lu,%lu,%lu",
      static_cast<unsigned long long>(s.uptime_ms),
      s.mode != nullptr ? s.mode : "none", static_cast<int>(s.rssi),
      static_cast<unsigned long>(s.heap_free_internal),
      static_cast<unsigned long>(s.heap_free_psram),
      static_cast<unsigned long>(s.peers), static_cast<unsigned>(s.playing),
      static_cast<unsigned long>(s.milli_bpm),
      static_cast<unsigned long>(s.pulse_edges),
      static_cast<unsigned long>(s.late_max_us),
      static_cast<unsigned long>(s.late_avg_us));
  return n > 0 && static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;
}

}  // namespace neon
