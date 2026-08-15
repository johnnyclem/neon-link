#include "neon/telemetry/emitter.hpp"

namespace neon {

const char* telemetry_mode_str(bool eth_active, bool ap_up, bool sta_up) {
  if (eth_active) {
    return "eth";
  }
  if (ap_up && sta_up) {
    return "apsta";
  }
  if (ap_up) {
    return "ap";
  }
  if (sta_up) {
    return "sta";
  }
  return "none";
}

TelemetrySample telemetry_sample_from_status(const AudioStatus& status,
                                             const char* mode,
                                             uint64_t uptime_ms) {
  TelemetrySample sample;
  sample.uptime_ms = uptime_ms;
  sample.mode = mode;
  sample.prio_set = status.priority_profile;
  sample.req_jitter_ms = status.req_jitter_ms;
  sample.eff_jitter_ms = status.eff_jitter_ms;
  sample.jit_fill_frames = status.fill_frames;
  sample.jit_underruns = status.jit_underruns;
  sample.jit_conceals = status.concealed;
  sample.jit_state = status.sub_state;
  sample.rx_dropped = status.rx_dropped;
  sample.rx_high_water = status.rx_high_water;
  sample.la_trim_ppm = status.trim_ppm;
  sample.i2s_write_failures = status.i2s_write_failures;
  sample.rssi = status.rssi;
  sample.heap_free_internal = status.heap_free_internal;
  sample.heap_free_psram = status.heap_free_psram;
  return sample;
}

TelemetryTick TelemetryTicker::tick(bool enabled) {
  TelemetryTick out;
  if (enabled && !was_enabled_) {
    out.want_header = true;
    countdown_ = 0;
  }
  was_enabled_ = enabled;
  if (!enabled) {
    countdown_ = 0;
    return out;
  }
  if (countdown_ == 0) {
    out.want_line = true;
    countdown_ = ticks_per_line_ - 1;
  } else {
    --countdown_;
  }
  return out;
}

}  // namespace neon
