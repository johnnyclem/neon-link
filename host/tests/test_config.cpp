#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "fake_storage.hpp"
#include "neon/config/model.hpp"
#include "neon/tempo_cv.hpp"

TEST_CASE("crc32 known vector") {
  // CRC-32 (IEEE) of "123456789" is 0xCBF43926.
  const char* s = "123456789";
  CHECK(neon::crc32(reinterpret_cast<const uint8_t*>(s), 9) == 0xcbf43926u);
}

TEST_CASE("config round-trips through storage") {
  neon::Config cfg;
  cfg.engine.clocks[0].ppqn = 24;
  cfg.engine.clocks[1].shuffle_pct = 33;
  cfg.engine.clocks[2].mode = neon::ClockOutputConfig::PulseMode::kSquare;
  cfg.engine.clocks[2].duty_pct = 10;
  cfg.engine.reset_mode = neon::ResetMode::kEveryBar;
  cfg.engine.latency_us = -1234;
  cfg.tempo_cv_min_bpm = 40;
  cfg.tempo_cv_max_bpm = 240;

  std::vector<uint8_t> buf(neon::config_blob_size());
  REQUIRE(neon::config_encode(cfg, buf.data(), buf.size()) == buf.size());

  fakes::FakeStorage storage;
  REQUIRE(storage.write_blob("cfg", buf.data(), buf.size()));

  std::vector<uint8_t> back(buf.size());
  size_t len = 0;
  REQUIRE(storage.read_blob("cfg", back.data(), back.size(), &len));

  neon::Config out;
  REQUIRE(neon::config_decode(back.data(), len, &out));
  CHECK(out.engine.clocks[0].ppqn == 24);
  CHECK(out.engine.clocks[1].shuffle_pct == 33);
  CHECK(out.engine.clocks[2].mode ==
        neon::ClockOutputConfig::PulseMode::kSquare);
  CHECK(out.engine.clocks[2].duty_pct == 10);
  CHECK(out.engine.reset_mode == neon::ResetMode::kEveryBar);
  CHECK(out.engine.latency_us == -1234);
  CHECK(out.tempo_cv_min_bpm == 40);
  CHECK(out.tempo_cv_max_bpm == 240);
}

TEST_CASE("config decode rejects corruption, bad magic, bad size") {
  neon::Config cfg;
  std::vector<uint8_t> buf(neon::config_blob_size());
  REQUIRE(neon::config_encode(cfg, buf.data(), buf.size()) == buf.size());

  neon::Config out;
  SUBCASE("bit flip in payload") {
    buf[20] ^= 0x40;
    CHECK_FALSE(neon::config_decode(buf.data(), buf.size(), &out));
  }
  SUBCASE("bad magic") {
    buf[0] ^= 0xff;
    CHECK_FALSE(neon::config_decode(buf.data(), buf.size(), &out));
  }
  SUBCASE("truncated") {
    CHECK_FALSE(neon::config_decode(buf.data(), buf.size() - 5, &out));
  }
  SUBCASE("intact still decodes") {
    CHECK(neon::config_decode(buf.data(), buf.size(), &out));
  }
}

TEST_CASE("config sanitize clamps out-of-range values") {
  neon::Config cfg;
  cfg.engine.clocks[0].ppqn = 100000;
  cfg.engine.clocks[0].mult = 0;
  cfg.engine.clocks[0].shuffle_pct = 99;
  cfg.engine.latency_us = 9999999;
  cfg.tempo_cv_min_bpm = 500;
  cfg.tempo_cv_max_bpm = 100;
  neon::config_sanitize(&cfg);
  CHECK(cfg.engine.clocks[0].ppqn == 192);
  CHECK(cfg.engine.clocks[0].mult == 1);
  CHECK(cfg.engine.clocks[0].shuffle_pct == 75);
  CHECK(cfg.engine.latency_us == 50000);
  CHECK(cfg.tempo_cv_max_bpm > cfg.tempo_cv_min_bpm);
}

TEST_CASE("tempo CV mapping is linear and clamped") {
  CHECK(neon::tempo_cv_ratio_q16(20000, 20, 300) == 0);
  CHECK(neon::tempo_cv_ratio_q16(10000, 20, 300) == 0);
  CHECK(neon::tempo_cv_ratio_q16(300000, 20, 300) == 65535);
  CHECK(neon::tempo_cv_ratio_q16(400000, 20, 300) == 65535);
  const uint16_t mid = neon::tempo_cv_ratio_q16(160000, 20, 300);
  CHECK(mid == 65535 / 2);
}

// --- Device identity and network settings ----------------------------

TEST_CASE("hostnames are reduced to a DNS-safe label") {
  char out[24];
  neon::sanitize_hostname("NEON Link", out, sizeof(out));
  CHECK(std::string(out) == "neon-link");
  neon::sanitize_hostname("  Studio  B!!  ", out, sizeof(out));
  CHECK(std::string(out) == "studio-b");
  neon::sanitize_hostname("a__b--c", out, sizeof(out));
  CHECK(std::string(out) == "a-b-c");
  neon::sanitize_hostname("rack.2", out, sizeof(out));
  CHECK(std::string(out) == "rack-2");
  // Nothing usable falls back rather than producing an empty hostname.
  neon::sanitize_hostname("!!!", out, sizeof(out));
  CHECK(std::string(out) == "neon-link");
  neon::sanitize_hostname("", out, sizeof(out));
  CHECK(std::string(out) == "neon-link");
}

TEST_CASE("access point SSID defaults to the device name plus MAC digits") {
  neon::Config cfg;
  const uint8_t mac[6] = {0xde, 0xad, 0xbe, 0xef, 0x12, 0x34};
  char ssid[33];
  neon::ap_ssid_for(cfg, mac, ssid, sizeof(ssid));
  CHECK(std::string(ssid) == "NEON-LINK-1234");

  std::strcpy(cfg.device_name, "stage-left");
  neon::ap_ssid_for(cfg, mac, ssid, sizeof(ssid));
  CHECK(std::string(ssid) == "STAGE-LEFT-1234");

  // An explicit SSID wins verbatim.
  std::strcpy(cfg.ap_ssid, "Backline Rig");
  neon::ap_ssid_for(cfg, mac, ssid, sizeof(ssid));
  CHECK(std::string(ssid) == "Backline Rig");
}

TEST_CASE("sanitize normalizes the device name in place") {
  neon::Config cfg;
  std::strcpy(cfg.device_name, "Studio B");
  neon::config_sanitize(&cfg);
  CHECK(std::string(cfg.device_name) == "studio-b");
}

TEST_CASE("an AP password shorter than WPA2 allows leaves the network open") {
  neon::Config cfg;
  cfg.ap_require_pass = 1;
  std::strcpy(cfg.ap_pass, "short");
  neon::config_sanitize(&cfg);
  CHECK(cfg.ap_require_pass == 0);

  std::strcpy(cfg.ap_pass, "longenough");
  cfg.ap_require_pass = 1;
  neon::config_sanitize(&cfg);
  CHECK(cfg.ap_require_pass == 1);
}

TEST_CASE("an empty WiFi slot never keeps a stale password") {
  neon::Config cfg;
  cfg.wifi[1].ssid[0] = '\0';
  std::strcpy(cfg.wifi[1].pass, "leftover");
  cfg.wifi[1].hidden = 1;
  neon::config_sanitize(&cfg);
  CHECK(cfg.wifi[1].pass[0] == '\0');
  CHECK(cfg.wifi[1].hidden == 0);
}

TEST_CASE("new settings survive an encode/decode round trip") {
  neon::Config a;
  std::strcpy(a.device_name, "tourbus");
  std::strcpy(a.wifi[2].ssid, "greenroom");
  std::strcpy(a.wifi[2].pass, "hunter2000");
  a.wifi_retries = 7;
  a.ap_policy = neon::ApPolicy::kOff;
  a.display_brightness = 12;
  a.midi_nudge_us = 4500;
  a.start_stop_sync = 0;
  a.tempo_milli_bpm = 174000;
  a.engine.clocks[2].role = neon::OutputRole::kGate;
  a.engine.clocks[3].step_mask = 0x0f0f0f0f0f0f0f0full;
  a.engine.reset_mode = neon::ResetMode::kAtStop;
  a.engine.reset_before_edge = true;

  std::vector<uint8_t> buf(neon::config_blob_size());
  REQUIRE(neon::config_encode(a, buf.data(), buf.size()) == buf.size());
  neon::Config b;
  REQUIRE(neon::config_decode(buf.data(), buf.size(), &b));

  CHECK(std::string(b.device_name) == "tourbus");
  CHECK(std::string(b.wifi[2].ssid) == "greenroom");
  CHECK(std::string(b.wifi[2].pass) == "hunter2000");
  CHECK(b.wifi_retries == 7);
  CHECK(b.ap_policy == neon::ApPolicy::kOff);
  CHECK(b.display_brightness == 12);
  CHECK(b.midi_nudge_us == 4500);
  CHECK(b.start_stop_sync == 0);
  CHECK(b.tempo_milli_bpm == 174000);
  CHECK(b.engine.clocks[2].role == neon::OutputRole::kGate);
  CHECK(b.engine.clocks[3].step_mask == 0x0f0f0f0f0f0f0f0full);
  CHECK(b.engine.reset_mode == neon::ResetMode::kAtStop);
  CHECK(b.engine.reset_before_edge);
}

TEST_CASE("out-of-range roles, tempo, and retries are clamped") {
  neon::Config cfg;
  cfg.engine.clocks[0].role = static_cast<neon::OutputRole>(99);
  cfg.tempo_milli_bpm = 1;
  cfg.wifi_retries = 0;
  cfg.midi_nudge_us = 9999999;
  cfg.ap_channel = 99;
  neon::config_sanitize(&cfg);
  CHECK(cfg.engine.clocks[0].role == neon::OutputRole::kClock);
  CHECK(cfg.tempo_milli_bpm == neon::kMinMilliBpm);
  CHECK(cfg.wifi_retries == 1);
  CHECK(cfg.midi_nudge_us == 100000);
  CHECK(cfg.ap_channel == 13);
}
