#include "neon/telemetry/csv.hpp"

#include <cstdio>

namespace neon {

size_t telemetry_csv_header(char* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const int n = std::snprintf(
      out, cap,
      "uptime_ms,mode,prio_set,req_jitter_ms,eff_jitter_ms,"
      "jit_fill_frames,jit_underruns,jit_conceals,jit_state,"
      "rx_dropped,rx_high_water,la_trim_ppm,"
      "i2s_write_failures,rssi,heap_free_internal,heap_free_psram");
  return n > 0 && static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;
}

size_t telemetry_csv_line(const TelemetrySample& s, char* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const int n = std::snprintf(
      out, cap, "%llu,%s,%u,%u,%u,%lu,%lu,%lu,%u,%lu,%lu,%ld,%lu,%d,%lu,%lu",
      static_cast<unsigned long long>(s.uptime_ms),
      s.mode != nullptr ? s.mode : "none", static_cast<unsigned>(s.prio_set),
      static_cast<unsigned>(s.req_jitter_ms),
      static_cast<unsigned>(s.eff_jitter_ms),
      static_cast<unsigned long>(s.jit_fill_frames),
      static_cast<unsigned long>(s.jit_underruns),
      static_cast<unsigned long>(s.jit_conceals),
      static_cast<unsigned>(s.jit_state),
      static_cast<unsigned long>(s.rx_dropped),
      static_cast<unsigned long>(s.rx_high_water),
      static_cast<long>(s.la_trim_ppm),
      static_cast<unsigned long>(s.i2s_write_failures),
      static_cast<int>(s.rssi),
      static_cast<unsigned long>(s.heap_free_internal),
      static_cast<unsigned long>(s.heap_free_psram));
  return n > 0 && static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;
}

}  // namespace neon
