#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "neon/telemetry/midi_pll_csv.hpp"

namespace {

using neon::MidiClockPll;

size_t count_fields(const char* s, size_t n) {
  size_t commas = 1;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] == ',') ++commas;
  }
  return commas;
}

}  // namespace

TEST_CASE("midi_pll CSV header and a hand-filled line agree field-for-field") {
  char header[128];
  const size_t hn = neon::midi_pll_telemetry_csv_header(header, sizeof(header));
  REQUIRE(hn > 0);
  CHECK(std::string(header, hn) ==
        "t_us,tick,transport,residual_us,tempo_milli_bpm,"
        "locked,playing,beat_valid,following");

  neon::MidiPllTelemetrySample s;
  s.t_us = 12345678901ll;
  s.tick = 4801;
  s.transport = "ble";
  s.residual_us = -137;
  s.tempo_milli_bpm = 128000;
  s.locked = 1;
  s.playing = 1;
  s.beat_valid = 0;
  s.following = 1;
  char line[128];
  const size_t ln = neon::midi_pll_telemetry_csv_line(s, line, sizeof(line));
  REQUIRE(ln > 0);
  CHECK(std::string(line, ln) == "12345678901,4801,ble,-137,128000,1,1,0,1");
  CHECK(count_fields(header, hn) == count_fields(line, ln));
}

TEST_CASE("midi_pll CSV: undersized buffer reports failure, not truncation") {
  char tiny[4];
  CHECK(neon::midi_pll_telemetry_csv_header(tiny, sizeof(tiny)) == 0);
  neon::MidiPllTelemetrySample s;
  CHECK(neon::midi_pll_telemetry_csv_line(s, tiny, sizeof(tiny)) == 0);
}

TEST_CASE("midi_pll transport strings cover the enum") {
  CHECK(std::string(neon::midi_pll_transport_str(
            MidiClockPll::Transport::kDin)) == "din");
  CHECK(std::string(neon::midi_pll_transport_str(
            MidiClockPll::Transport::kUsb)) == "usb");
  CHECK(std::string(neon::midi_pll_transport_str(
            MidiClockPll::Transport::kBle)) == "ble");
}

TEST_CASE("sample from a live PLL: seeded state, lock, and transport anchor") {
  MidiClockPll pll;
  const int64_t period = llround(60000000.0 / (24.0 * 120.0));

  // Before the seed window fills, tempo reads 0 and nothing is locked.
  int64_t t = 0;
  for (int i = 0; i < 4; ++i, t += period) {
    pll.on_tick(t);
  }
  neon::MidiPllTelemetrySample s =
      neon::midi_pll_telemetry_sample(pll, t - period, /*following=*/false);
  CHECK(s.tick == 4);
  CHECK(s.transport == std::string("din"));
  CHECK(s.tempo_milli_bpm == 0);
  CHECK(s.locked == 0);
  CHECK(s.beat_valid == 0);
  CHECK(s.following == 0);

  // Start lands on the next tick; a clean beat of ticks locks the loop.
  pll.on_start(t);
  for (int i = 0; i < 60; ++i, t += period) {
    pll.on_tick(t);
  }
  s = neon::midi_pll_telemetry_sample(pll, t - period, /*following=*/true);
  CHECK(s.tick == 64);
  CHECK(s.locked == 1);
  CHECK(s.playing == 1);
  CHECK(s.beat_valid == 1);
  CHECK(s.following == 1);
  CHECK(s.tempo_milli_bpm >= 119900);
  CHECK(s.tempo_milli_bpm <= 120100);
  CHECK(s.residual_us >= -50);
  CHECK(s.residual_us <= 50);
}
