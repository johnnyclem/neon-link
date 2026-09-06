#include <doctest.h>

#include <cstdint>
#include <string>

#include "neon/telemetry/audio_follow_csv.hpp"

namespace {

size_t count_fields(const char* s, size_t n) {
  size_t commas = 1;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] == ',') ++commas;
  }
  return commas;
}

}  // namespace

TEST_CASE("AFOL CSV header and a hand-filled line agree field-for-field") {
  char header[128];
  const size_t hn =
      neon::audio_follow_telemetry_csv_header(header, sizeof(header));
  REQUIRE(hn > 0);
  CHECK(std::string(header, hn) ==
        "t_us,lock,subdiv,onset_hz_x10,est_mbpm,pub_mbpm,onsets,rejects,"
        "following");

  neon::FollowStatus st;
  st.lock = 2;
  st.subdiv = 2;
  st.onset_hz_x10 = 39;
  st.mbpm = 118000;
  st.published_mbpm = 118000;
  st.onsets = 12;
  st.rejects = 3;
  const neon::AudioFollowTelemetrySample s =
      neon::audio_follow_telemetry_sample(st, 12345678901ll, /*following=*/true);
  char line[128];
  const size_t ln =
      neon::audio_follow_telemetry_csv_line(s, line, sizeof(line));
  REQUIRE(ln > 0);
  CHECK(std::string(line, ln) == "12345678901,2,2,39,118000,118000,12,3,1");
  CHECK(count_fields(header, hn) == count_fields(line, ln));
}

TEST_CASE("AFOL CSV: undersized buffer reports failure, not truncation") {
  char tiny[4];
  CHECK(neon::audio_follow_telemetry_csv_header(tiny, sizeof(tiny)) == 0);
  neon::AudioFollowTelemetrySample s;
  CHECK(neon::audio_follow_telemetry_csv_line(s, tiny, sizeof(tiny)) == 0);
}
