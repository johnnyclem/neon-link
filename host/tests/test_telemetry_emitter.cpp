#include <doctest.h>

#include <string>

#include "neon/telemetry/emitter.hpp"

TEST_CASE("telemetry_mode_str: ethernet wins outright") {
  CHECK(std::string(neon::telemetry_mode_str(true, false, false)) == "eth");
  CHECK(std::string(neon::telemetry_mode_str(true, true, true)) == "eth");
}

TEST_CASE("telemetry_mode_str: AP and STA are independent") {
  CHECK(std::string(neon::telemetry_mode_str(false, true, true)) == "apsta");
  CHECK(std::string(neon::telemetry_mode_str(false, true, false)) == "ap");
  CHECK(std::string(neon::telemetry_mode_str(false, false, true)) == "sta");
  CHECK(std::string(neon::telemetry_mode_str(false, false, false)) == "none");
}

TEST_CASE("telemetry_sample_from_status maps every field, not just some") {
  neon::AudioStatus st{};
  st.priority_profile = 1;
  st.req_jitter_ms = 200;
  st.eff_jitter_ms = 170;
  st.fill_frames = 8160;
  st.jit_underruns = 3;
  st.concealed = 2;
  st.sub_state = 2;
  st.rx_dropped = 5;
  st.rx_high_water = 42;
  st.trim_ppm = -38;
  st.i2s_write_failures = 1;
  st.rssi = -67;
  st.heap_free_internal = 123000;
  st.heap_free_psram = 4500000;

  const neon::TelemetrySample s =
      neon::telemetry_sample_from_status(st, "ap", 123456789ull);

  CHECK(s.uptime_ms == 123456789ull);
  CHECK(std::string(s.mode) == "ap");
  CHECK(s.prio_set == st.priority_profile);
  CHECK(s.req_jitter_ms == st.req_jitter_ms);
  CHECK(s.eff_jitter_ms == st.eff_jitter_ms);
  CHECK(s.jit_fill_frames == st.fill_frames);
  CHECK(s.jit_underruns == st.jit_underruns);
  CHECK(s.jit_conceals == st.concealed);
  CHECK(s.jit_state == st.sub_state);
  CHECK(s.rx_dropped == st.rx_dropped);
  CHECK(s.rx_high_water == st.rx_high_water);
  CHECK(s.la_trim_ppm == st.trim_ppm);
  CHECK(s.i2s_write_failures == st.i2s_write_failures);
  CHECK(s.rssi == st.rssi);
  CHECK(s.heap_free_internal == st.heap_free_internal);
  CHECK(s.heap_free_psram == st.heap_free_psram);
}

TEST_CASE("TelemetryTicker: no header or line while disabled") {
  neon::TelemetryTicker t(4);
  for (int i = 0; i < 10; ++i) {
    const neon::TelemetryTick tick = t.tick(false);
    CHECK_FALSE(tick.want_header);
    CHECK_FALSE(tick.want_line);
  }
}

TEST_CASE("TelemetryTicker: enabling emits a header and a line on the same tick") {
  neon::TelemetryTicker t(4);
  const neon::TelemetryTick tick = t.tick(true);
  CHECK(tick.want_header);
  CHECK(tick.want_line);
}

TEST_CASE("TelemetryTicker: lines are throttled to one per ticks_per_line") {
  neon::TelemetryTicker t(4);
  int lines = 0;
  int headers = 0;
  for (int i = 0; i < 12; ++i) {
    const neon::TelemetryTick tick = t.tick(true);
    if (tick.want_header) ++headers;
    if (tick.want_line) ++lines;
  }
  // Ticks 1, 5, 9 want a line (three in twelve ticks at period 4); only
  // tick 1 (the enable edge) wants a header.
  CHECK(headers == 1);
  CHECK(lines == 3);
}

TEST_CASE("TelemetryTicker: re-enabling reprints the header and does not wait out a stale countdown") {
  neon::TelemetryTicker t(4);
  CHECK(t.tick(true).want_header);
  CHECK_FALSE(t.tick(true).want_header);  // tick 2: mid-cycle, no header
  CHECK_FALSE(t.tick(true).want_line);    // tick 2 also has no line yet

  CHECK_FALSE(t.tick(false).want_header);  // disable mid-cycle
  const neon::TelemetryTick re_enabled = t.tick(true);
  CHECK(re_enabled.want_header);
  CHECK(re_enabled.want_line);  // not stuck waiting out the old countdown
}

TEST_CASE("TelemetryTicker: 0 ticks_per_line emits every tick instead of dividing by zero") {
  neon::TelemetryTicker t(0);
  for (int i = 0; i < 5; ++i) {
    CHECK(t.tick(true).want_line);
  }
}
