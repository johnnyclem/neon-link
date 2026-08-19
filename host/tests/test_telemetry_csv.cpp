#include <doctest.h>

#include <cstring>
#include <string>

#include "neon/telemetry/csv.hpp"
#include "neon/telemetry/linksync_csv.hpp"

TEST_CASE("telemetry CSV header matches the plan's field order") {
  char buf[256];
  const size_t n = neon::telemetry_csv_header(buf, sizeof(buf));
  REQUIRE(n > 0);
  CHECK(std::string(buf, n) ==
        "uptime_ms,mode,prio_set,req_jitter_ms,eff_jitter_ms,"
        "jit_fill_frames,jit_underruns,jit_conceals,jit_state,"
        "rx_dropped,rx_high_water,la_trim_ppm,"
        "i2s_write_failures,rssi,heap_free_internal,heap_free_psram");
}

TEST_CASE("telemetry CSV line: field count matches the header, values round-trip") {
  neon::TelemetrySample s;
  s.uptime_ms = 123456789ull;
  s.mode = "ap";
  s.prio_set = 1;
  s.req_jitter_ms = 200;
  s.eff_jitter_ms = 170;
  s.jit_fill_frames = 8160;
  s.jit_underruns = 3;
  s.jit_conceals = 2;
  s.jit_state = 2;
  s.rx_dropped = 5;
  s.rx_high_water = 42;
  s.la_trim_ppm = -38;
  s.i2s_write_failures = 1;
  s.rssi = -67;
  s.heap_free_internal = 123000;
  s.heap_free_psram = 4500000;

  char header[256];
  char line[256];
  const size_t hn = neon::telemetry_csv_header(header, sizeof(header));
  const size_t ln = neon::telemetry_csv_line(s, line, sizeof(line));
  REQUIRE(hn > 0);
  REQUIRE(ln > 0);

  auto count_fields = [](const char* s, size_t n) {
    size_t commas = 1;
    for (size_t i = 0; i < n; ++i) {
      if (s[i] == ',') ++commas;
    }
    return commas;
  };
  CHECK(count_fields(header, hn) == count_fields(line, ln));

  CHECK(std::string(line, ln) ==
        "123456789,ap,1,200,170,8160,3,2,2,5,42,-38,1,-67,123000,4500000");
}

TEST_CASE("link-sync telemetry CSV header and a sample line") {
  char header[192];
  const size_t hn = neon::linksync_telemetry_csv_header(header, sizeof(header));
  REQUIRE(hn > 0);
  CHECK(std::string(header, hn) ==
        "uptime_ms,mode,rssi,heap_free_internal,heap_free_psram,"
        "peers,playing,milli_bpm,pulse_edges,late_max_us,late_avg_us");

  neon::LinkSyncTelemetrySample s;
  s.uptime_ms = 15000;
  s.mode = "ap";
  s.rssi = 0;
  s.heap_free_internal = 200000;
  s.heap_free_psram = 7000000;
  s.peers = 2;
  s.playing = 1;
  s.milli_bpm = 128000;
  s.pulse_edges = 480;
  s.late_max_us = 12;
  s.late_avg_us = 3;
  char line[192];
  const size_t ln = neon::linksync_telemetry_csv_line(s, line, sizeof(line));
  REQUIRE(ln > 0);
  CHECK(std::string(line, ln) ==
        "15000,ap,0,200000,7000000,2,1,128000,480,12,3");
}

TEST_CASE("telemetry CSV: undersized buffer reports failure, not truncation") {
  char tiny[4];
  CHECK(neon::telemetry_csv_header(tiny, sizeof(tiny)) == 0);
  neon::TelemetrySample s;
  CHECK(neon::telemetry_csv_line(s, tiny, sizeof(tiny)) == 0);
}
