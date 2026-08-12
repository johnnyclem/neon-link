#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "neon/config/json.hpp"

namespace {
std::string encode(const neon::Config& cfg) {
  std::vector<char> buf(4096);
  const size_t n = neon::config_to_json(cfg, buf.data(), buf.size());
  REQUIRE(n > 0);
  return std::string(buf.data(), n);
}
}  // namespace

TEST_CASE("config JSON round-trips every field") {
  neon::Config a;
  a.engine.clocks[0].ppqn = 24;
  a.engine.clocks[1].mode = neon::ClockOutputConfig::PulseMode::kSquare;
  a.engine.clocks[1].duty_pct = 30;
  a.engine.clocks[2].shuffle_pct = 42;
  a.engine.clocks[3].enabled = false;
  a.engine.reset_mode = neon::ResetMode::kEveryBar;
  a.engine.latency_us = -4200;
  a.engine.transport_gating = true;
  a.tempo_cv_min_bpm = 60;
  a.tempo_cv_max_bpm = 180;
  a.quantum_beats = 8;
  a.clock_source = neon::ClockSource::kExternalMaster;
  a.clock_in_ppqn = 24;
  a.ble_enabled = 0;
  a.midi.midi_channel = 5;
  a.midi.gate_target = 3;
  a.midi.pitch_cv = true;
  a.midi.clock_policy = neon::MidiRouteConfig::ClockPolicy::kMerge;
  std::strcpy(a.wifi[0].ssid, "studio");
  std::strcpy(a.wifi[0].pass, "secret123");
  std::strcpy(a.wifi[1].ssid, "rehearsal");
  a.wifi[1].hidden = 1;
  a.wifi_retries = 5;
  a.ap_policy = neon::ApPolicy::kAlways;
  a.ap_hidden = 1;
  std::strcpy(a.ap_ssid, "backline");
  std::strcpy(a.device_name, "stage-left");
  a.display_brightness = 64;
  a.midi_nudge_us = -3000;
  a.start_stop_sync = 0;
  a.tempo_milli_bpm = 137500;

  const std::string json = encode(a);
  neon::Config b;
  REQUIRE(neon::config_from_json(json.c_str(), json.size(), &b));

  CHECK(b.engine.clocks[0].ppqn == 24);
  CHECK(b.engine.clocks[1].mode ==
        neon::ClockOutputConfig::PulseMode::kSquare);
  CHECK(b.engine.clocks[1].duty_pct == 30);
  CHECK(b.engine.clocks[2].shuffle_pct == 42);
  CHECK_FALSE(b.engine.clocks[3].enabled);
  CHECK(b.engine.reset_mode == neon::ResetMode::kEveryBar);
  CHECK(b.engine.latency_us == -4200);
  CHECK(b.engine.transport_gating);
  CHECK(b.tempo_cv_min_bpm == 60);
  CHECK(b.tempo_cv_max_bpm == 180);
  CHECK(b.quantum_beats == 8);
  CHECK(b.clock_source == neon::ClockSource::kExternalMaster);
  CHECK(b.clock_in_ppqn == 24);
  CHECK(b.ble_enabled == 0);
  CHECK(b.midi.midi_channel == 5);
  CHECK(b.midi.gate_target == 3);
  CHECK(b.midi.pitch_cv);
  CHECK(b.midi.clock_policy == neon::MidiRouteConfig::ClockPolicy::kMerge);
  CHECK(std::strcmp(b.wifi[0].ssid, "studio") == 0);
  CHECK(std::strcmp(b.wifi[1].ssid, "rehearsal") == 0);
  CHECK(b.wifi[1].hidden == 1);
  CHECK(b.wifi_retries == 5);
  CHECK(b.ap_policy == neon::ApPolicy::kAlways);
  CHECK(b.ap_hidden == 1);
  CHECK(std::strcmp(b.ap_ssid, "backline") == 0);
  CHECK(std::strcmp(b.device_name, "stage-left") == 0);
  CHECK(b.display_brightness == 64);
  CHECK(b.midi_nudge_us == -3000);
  CHECK(b.start_stop_sync == 0);
  CHECK(b.tempo_milli_bpm == 137500);
}

TEST_CASE("passwords are write-only: encode never leaks them") {
  neon::Config a;
  std::strcpy(a.wifi[0].ssid, "studio");
  std::strcpy(a.wifi[0].pass, "supersecret");
  std::strcpy(a.ap_pass, "apsecret1");
  const std::string json = encode(a);
  CHECK(json.find("supersecret") == std::string::npos);
  CHECK(json.find("apsecret1") == std::string::npos);
  CHECK(json.find("\"has_pass\":true") != std::string::npos);

  // Round-tripping the encoded doc (empty pass) preserves the stored ones.
  neon::Config b = a;
  REQUIRE(neon::config_from_json(json.c_str(), json.size(), &b));
  CHECK(std::strcmp(b.wifi[0].pass, "supersecret") == 0);
  CHECK(std::strcmp(b.ap_pass, "apsecret1") == 0);
}

TEST_CASE("changing a stored SSID clears that slot's password") {
  neon::Config cfg;
  std::strcpy(cfg.wifi[0].ssid, "studio");
  std::strcpy(cfg.wifi[0].pass, "oldsecret");

  const char* doc = R"({"wifi":{"networks":[{"ssid":"tourbus","pass":""}]}})";
  REQUIRE(neon::config_from_json(doc, std::strlen(doc), &cfg));
  CHECK(std::strcmp(cfg.wifi[0].ssid, "tourbus") == 0);
  CHECK(cfg.wifi[0].pass[0] == '\0');
}

TEST_CASE("64-step pattern masks survive the JSON round trip") {
  neon::Config a;
  a.engine.clocks[0].rhythm = neon::ClockOutputConfig::RhythmMode::kPattern;
  a.engine.clocks[0].step_mask = 0x8000000000000001ull;
  a.engine.clocks[0].rhythm_over_loop = true;
  a.engine.clocks[1].role = neon::OutputRole::kResetStop;
  a.engine.clocks[2].role = neon::OutputRole::kGate;
  a.engine.clocks[3].free_run = true;
  a.engine.reset_before_edge = true;
  a.engine.reset_lead_us = 2500;

  const std::string json = encode(a);
  neon::Config b;
  REQUIRE(neon::config_from_json(json.c_str(), json.size(), &b));
  CHECK(b.engine.clocks[0].rhythm ==
        neon::ClockOutputConfig::RhythmMode::kPattern);
  CHECK(b.engine.clocks[0].step_mask == 0x8000000000000001ull);
  CHECK(b.engine.clocks[0].rhythm_over_loop);
  CHECK(b.engine.clocks[1].role == neon::OutputRole::kResetStop);
  CHECK(b.engine.clocks[2].role == neon::OutputRole::kGate);
  CHECK(b.engine.clocks[3].free_run);
  CHECK(b.engine.reset_before_edge);
  CHECK(b.engine.reset_lead_us == 2500);
}

TEST_CASE("partial update touches only the fields present") {
  neon::Config cfg;
  cfg.engine.clocks[0].ppqn = 24;
  cfg.engine.latency_us = 1000;
  cfg.quantum_beats = 8;

  const char* patch = R"({"engine":{"latency_us":-500}})";
  REQUIRE(neon::config_from_json(patch, std::strlen(patch), &cfg));
  CHECK(cfg.engine.latency_us == -500);
  CHECK(cfg.engine.clocks[0].ppqn == 24);  // untouched
  CHECK(cfg.quantum_beats == 8);           // untouched
}

TEST_CASE("malformed and wrong-type JSON are rejected untouched") {
  neon::Config cfg;
  cfg.quantum_beats = 8;
  const char* bad1 = "{not json";
  const char* bad2 = "[1,2,3]";
  CHECK_FALSE(neon::config_from_json(bad1, std::strlen(bad1), &cfg));
  CHECK_FALSE(neon::config_from_json(bad2, std::strlen(bad2), &cfg));
  CHECK(cfg.quantum_beats == 8);
}

TEST_CASE("unknown fields are ignored, out-of-range values sanitized") {
  neon::Config cfg;
  const char* doc =
      R"({"mystery":true,"engine":{"clocks":[{"ppqn":100000,"shuffle_pct":99}],)"
      R"("latency_us":9999999},"clock_in_ppqn":5000})";
  REQUIRE(neon::config_from_json(doc, std::strlen(doc), &cfg));
  CHECK(cfg.engine.clocks[0].ppqn == 192);
  CHECK(cfg.engine.clocks[0].shuffle_pct == 75);
  CHECK(cfg.engine.latency_us == 50000);
  CHECK(cfg.clock_in_ppqn == 96);
}

TEST_CASE("enum strings decode case-sensitively and reject unknowns") {
  neon::Config cfg;
  const char* doc = R"({"clock_source":"external","engine":{"reset_mode":"off"}})";
  REQUIRE(neon::config_from_json(doc, std::strlen(doc), &cfg));
  CHECK(cfg.clock_source == neon::ClockSource::kExternalMaster);
  CHECK(cfg.engine.reset_mode == neon::ResetMode::kOff);

  // Unknown enum string: field keeps its previous value.
  const char* doc2 = R"({"clock_source":"warp"})";
  REQUIRE(neon::config_from_json(doc2, std::strlen(doc2), &cfg));
  CHECK(cfg.clock_source == neon::ClockSource::kExternalMaster);
}
