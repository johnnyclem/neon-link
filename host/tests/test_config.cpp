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

TEST_CASE("the AP requires a password out of the box") {
  neon::Config cfg;
  CHECK(cfg.ap_require_pass == 1);
  CHECK(std::string(cfg.ap_pass) == neon::kDefaultApPass);
}

TEST_CASE("an AP password shorter than WPA2 allows fails closed") {
  neon::Config cfg;
  cfg.ap_require_pass = 1;
  std::strcpy(cfg.ap_pass, "short");
  neon::config_sanitize(&cfg);
  // The network stays secured; the unusable key is replaced, not obeyed.
  CHECK(cfg.ap_require_pass == 1);
  CHECK(std::string(cfg.ap_pass) == neon::kDefaultApPass);

  std::strcpy(cfg.ap_pass, "longenough");
  cfg.ap_require_pass = 1;
  neon::config_sanitize(&cfg);
  CHECK(cfg.ap_require_pass == 1);
  CHECK(std::string(cfg.ap_pass) == "longenough");
}

TEST_CASE("the AP password is derived per-device from the MAC, not a "
          "fleet-wide constant") {
  char pass[33];
  const uint8_t mac_a[6] = {0xde, 0xad, 0xbe, 0xef, 0x12, 0x34};
  const uint8_t mac_b[6] = {0xde, 0xad, 0xbe, 0xaa, 0xbb, 0xcc};
  neon::derive_ap_pass_from_mac(mac_a, pass, sizeof(pass));
  // Long enough for WPA2 (8 chars) with room to spare.
  CHECK(std::strlen(pass) >= 8);
  CHECK(std::string(pass) == "link-EF1234");

  char pass_b[33];
  neon::derive_ap_pass_from_mac(mac_b, pass_b, sizeof(pass_b));
  CHECK(std::string(pass) != std::string(pass_b));

  // A device_name-only difference (mac_a vs mac_a again) reproduces the
  // same password: it must be deterministic, not re-randomized per call.
  char pass_again[33];
  neon::derive_ap_pass_from_mac(mac_a, pass_again, sizeof(pass_again));
  CHECK(std::string(pass) == std::string(pass_again));
}

TEST_CASE("device_token is never accepted from config_sanitize's caller "
          "unset, but survives once set") {
  neon::Config cfg;
  CHECK(cfg.device_token[0] == '\0');
  std::strcpy(cfg.device_token, "0123456789abcdef0123456789abcdef");
  neon::config_sanitize(&cfg);
  // Sanitize truncates to the field width; it does not clear a set token.
  CHECK(cfg.device_token[0] != '\0');
  CHECK(std::strlen(cfg.device_token) == sizeof(cfg.device_token) - 1);
}

TEST_CASE("a v5 config blob keeps its settings and defaults device_token") {
  neon::Config a;
  std::strcpy(a.wifi[0].ssid, "greenroom");
  a.tempo_milli_bpm = 128000;
  // A v5 firmware could never have written a real token here; whatever
  // lands in this field from decoding a v5-sized payload is tail padding,
  // not a secret worth honoring.
  std::strcpy(a.device_token, "deadbeefdeadbeefdeadbeefdeadbeef");

  std::vector<uint8_t> full(neon::config_blob_size());
  REQUIRE(neon::config_encode(a, full.data(), full.size()) == full.size());

  struct Hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t crc;
  };
  Hdr h;
  std::memcpy(&h, full.data(), sizeof(h));
  h.version = 5;
  h.crc = neon::crc32(full.data() + sizeof(h), h.payload_size);
  std::memcpy(full.data(), &h, sizeof(h));

  neon::Config b;
  REQUIRE(neon::config_decode(full.data(), full.size(), &b));
  CHECK(std::string(b.wifi[0].ssid) == "greenroom");
  CHECK(b.tempo_milli_bpm == 128000);
  CHECK(b.device_token[0] == '\0');
}

TEST_CASE("priority_profile sanitizes to a known enumerator") {
  neon::Config cfg;
  CHECK(cfg.priority_profile == neon::PriorityProfile::kFixed);

  cfg.priority_profile = neon::PriorityProfile::kLegacy;
  neon::config_sanitize(&cfg);
  CHECK(cfg.priority_profile == neon::PriorityProfile::kLegacy);

  // A garbage byte (a corrupt NVS blob, say) falls back to the safe
  // default rather than being read as some other enumerator.
  cfg.priority_profile = static_cast<neon::PriorityProfile>(0xaa);
  neon::config_sanitize(&cfg);
  CHECK(cfg.priority_profile == neon::PriorityProfile::kFixed);
}

// docs/STUDIO_MODE_TEST_PLAN.md Phase 2: kFixed must reproduce the shipped
// fix's numbers (asio above the Link Audio pump) and kLegacy must
// reproduce the pre-fix inversion it corrected — both pulled out as pure
// functions specifically so this is checkable without FreeRTOS.
TEST_CASE("link task priorities: fixed keeps asio above the pump, legacy inverts it") {
  const int asio_fixed = neon::link_asio_task_priority(neon::PriorityProfile::kFixed);
  const int pump_fixed = neon::link_pump_task_priority(neon::PriorityProfile::kFixed);
  CHECK(asio_fixed > pump_fixed);

  const int asio_legacy = neon::link_asio_task_priority(neon::PriorityProfile::kLegacy);
  const int pump_legacy = neon::link_pump_task_priority(neon::PriorityProfile::kLegacy);
  CHECK(asio_legacy < pump_legacy);
}

TEST_CASE("priority_profile_str: the single source of truth for both JSON producers") {
  CHECK(std::string(neon::priority_profile_str(neon::PriorityProfile::kFixed)) ==
        "fixed");
  CHECK(std::string(neon::priority_profile_str(neon::PriorityProfile::kLegacy)) ==
        "legacy");
}

TEST_CASE("network_identity_changed sees policy and slots, not retries") {
  neon::Config a;
  neon::Config b = a;
  CHECK_FALSE(neon::network_identity_changed(a, b));
  b.wifi_retries = 9;
  CHECK_FALSE(neon::network_identity_changed(a, b));
  b = a;
  b.ap_policy = neon::ApPolicy::kAlways;
  CHECK(neon::network_identity_changed(a, b));
  b = a;
  std::strcpy(b.wifi[0].ssid, "clemhaus");
  CHECK(neon::network_identity_changed(a, b));
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
  a.big_beat_display = 0;
  a.beat_style = neon::BeatStyle::kPie;
  a.midi_trs_type = 1;
  a.color_theme = neon::ColorTheme::kAmber;

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
  CHECK(b.big_beat_display == 0);
  CHECK(b.beat_style == neon::BeatStyle::kPie);
  CHECK(b.midi_trs_type == 1);
  CHECK(b.color_theme == neon::ColorTheme::kAmber);
}

TEST_CASE("a v2 config blob keeps wifi and defaults the big beat flag") {
  neon::Config a;
  std::strcpy(a.wifi[0].ssid, "clemhaus-IoT");
  std::strcpy(a.wifi[0].pass, "twelvechars!");
  a.big_beat_display = 0;

  std::vector<uint8_t> full(neon::config_blob_size());
  REQUIRE(neon::config_encode(a, full.data(), full.size()) == full.size());

  struct Hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t crc;
  };
  Hdr h;
  std::memcpy(&h, full.data(), sizeof(h));
  h.version = 2;
  h.crc = neon::crc32(full.data() + sizeof(h), h.payload_size);
  std::memcpy(full.data(), &h, sizeof(h));

  neon::Config b;
  REQUIRE(neon::config_decode(full.data(), full.size(), &b));
  CHECK(std::string(b.wifi[0].ssid) == "clemhaus-IoT");
  CHECK(std::string(b.wifi[0].pass) == "twelvechars!");
  CHECK(b.big_beat_display == 1);
}

TEST_CASE("out-of-range roles, tempo, and retries are clamped") {
  neon::Config cfg;
  cfg.beat_style = static_cast<neon::BeatStyle>(99);
  cfg.color_theme = static_cast<neon::ColorTheme>(99);
  cfg.engine.clocks[0].role = static_cast<neon::OutputRole>(99);
  cfg.tempo_milli_bpm = 1;
  cfg.wifi_retries = 0;
  cfg.midi_nudge_us = 9999999;
  cfg.ap_channel = 99;
  neon::config_sanitize(&cfg);
  CHECK(cfg.beat_style == neon::BeatStyle::kNumber);
  CHECK(cfg.color_theme == neon::ColorTheme::kTeal);
  CHECK(cfg.engine.clocks[0].role == neon::OutputRole::kClock);
  CHECK(cfg.tempo_milli_bpm == neon::kMinMilliBpm);
  CHECK(cfg.wifi_retries == 1);
  CHECK(cfg.midi_nudge_us == 100000);
  CHECK(cfg.ap_channel == 13);
}

TEST_CASE("sanitize restores unity click gain when the metro is armed") {
  neon::Config cfg;
  cfg.audio.metro_enabled = 1;
  cfg.audio.metro_gain = 0;
  neon::config_sanitize(&cfg);
  CHECK(cfg.audio.metro_gain == neon::kUnityGainByte);
}

TEST_CASE("audio defaults are off and quiet") {
  const neon::Config cfg;
  CHECK(cfg.audio.enabled == 0);
  CHECK(cfg.audio.metro_enabled == 0);
  CHECK(cfg.audio.role_l == neon::AudioRole::kMix);
  CHECK(cfg.audio.role_r == neon::AudioRole::kMix);
  CHECK(cfg.audio.metro_gain == neon::kUnityGainByte);
  CHECK(cfg.audio.linein_monitor_gain == 0);
  CHECK(cfg.audio.la_publish_mix == 0);
  CHECK(cfg.audio.la_fullband == 0);
  CHECK(cfg.audio.la_jitter_ms == 60);
  CHECK(cfg.audio.la_channel_name[0] == '\0');
  CHECK(cfg.audio.la_sub_channel_id[0] == '\0');
}

TEST_CASE("the audio block survives an encode/decode round trip") {
  neon::Config a;
  a.audio.enabled = 1;
  a.audio.role_l = neon::AudioRole::kMix;
  a.audio.role_r = neon::AudioRole::kClock;
  a.audio.metro_enabled = 1;
  a.audio.metro_sound = neon::ClickSound::kWood;
  a.audio.metro_gain = 137;
  a.audio.metro_accent = 0;
  a.audio.amy_enabled = 1;
  a.audio.amy_patch = 2;
  a.audio.linein_monitor_gain = 90;
  a.audio.la_publish_mix = 1;
  a.audio.la_publish_mono = 1;
  a.audio.la_fullband = 1;
  a.audio.la_jitter_ms = 120;
  std::strcpy(a.audio.la_channel_name, "tourbus");
  std::strcpy(a.audio.la_sub_channel_id, "peer:1234/Live Master");

  std::vector<uint8_t> buf(neon::config_blob_size());
  REQUIRE(neon::config_encode(a, buf.data(), buf.size()) == buf.size());
  neon::Config b;
  REQUIRE(neon::config_decode(buf.data(), buf.size(), &b));

  CHECK(b.audio.enabled == 1);
  CHECK(b.audio.role_r == neon::AudioRole::kClock);
  CHECK(b.audio.metro_sound == neon::ClickSound::kWood);
  CHECK(b.audio.metro_gain == 137);
  CHECK(b.audio.metro_accent == 0);
  CHECK(b.audio.amy_patch == 2);
  CHECK(b.audio.linein_monitor_gain == 90);
  CHECK(b.audio.la_publish_mix == 1);
  CHECK(b.audio.la_publish_mono == 1);
  CHECK(b.audio.la_fullband == 1);
  CHECK(b.audio.la_jitter_ms == 120);
  CHECK(std::string(b.audio.la_channel_name) == "tourbus");
  CHECK(std::string(b.audio.la_sub_channel_id) == "peer:1234/Live Master");
}

TEST_CASE("a v3 config blob keeps its settings and defaults the audio block") {
  neon::Config a;
  std::strcpy(a.wifi[0].ssid, "greenroom");
  a.big_beat_display = 0;
  a.tempo_milli_bpm = 143000;
  // Values a v3 blob could never have carried: the migration must wipe
  // them, because whatever a v3 payload puts here is its tail padding.
  a.audio.enabled = 1;
  a.audio.metro_gain = 3;
  a.audio.la_jitter_ms = 500;
  std::strcpy(a.audio.la_sub_channel_id, "stale");

  std::vector<uint8_t> full(neon::config_blob_size());
  REQUIRE(neon::config_encode(a, full.data(), full.size()) == full.size());

  struct Hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t crc;
  };
  Hdr h;
  std::memcpy(&h, full.data(), sizeof(h));
  h.version = 3;
  h.crc = neon::crc32(full.data() + sizeof(h), h.payload_size);
  std::memcpy(full.data(), &h, sizeof(h));

  neon::Config b;
  REQUIRE(neon::config_decode(full.data(), full.size(), &b));
  CHECK(std::string(b.wifi[0].ssid) == "greenroom");
  CHECK(b.big_beat_display == 0);
  CHECK(b.tempo_milli_bpm == 143000);
  CHECK(b.audio.enabled == 0);
  CHECK(b.audio.metro_gain == neon::kUnityGainByte);
  CHECK(b.audio.la_jitter_ms == 60);
  CHECK(b.audio.la_sub_channel_id[0] == '\0');
}

TEST_CASE("a v8 config blob defaults the colour theme to teal") {
  neon::Config a;
  a.color_theme = neon::ColorTheme::kPaper;

  std::vector<uint8_t> full(neon::config_blob_size());
  REQUIRE(neon::config_encode(a, full.data(), full.size()) == full.size());

  struct Hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t crc;
  };
  Hdr h;
  std::memcpy(&h, full.data(), sizeof(h));
  h.version = 8;
  h.crc = neon::crc32(full.data() + sizeof(h), h.payload_size);
  std::memcpy(full.data(), &h, sizeof(h));

  neon::Config b;
  REQUIRE(neon::config_decode(full.data(), full.size(), &b));
  CHECK(b.color_theme == neon::ColorTheme::kTeal);
}

TEST_CASE("a v11 config blob defaults display_portrait to landscape") {
  neon::Config a;
  a.display_portrait = 1;

  std::vector<uint8_t> full(neon::config_blob_size());
  REQUIRE(neon::config_encode(a, full.data(), full.size()) == full.size());

  struct Hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t crc;
  };
  Hdr h;
  std::memcpy(&h, full.data(), sizeof(h));
  h.version = 11;
  h.crc = neon::crc32(full.data() + sizeof(h), h.payload_size);
  std::memcpy(full.data(), &h, sizeof(h));

  neon::Config b;
  REQUIRE(neon::config_decode(full.data(), full.size(), &b));
  CHECK(b.display_portrait == 0);
}

TEST_CASE("display_portrait survives a current-version round trip") {
  neon::Config a;
  a.display_portrait = 1;
  std::vector<uint8_t> buf(neon::config_blob_size());
  REQUIRE(neon::config_encode(a, buf.data(), buf.size()) == buf.size());
  neon::Config b;
  REQUIRE(neon::config_decode(buf.data(), buf.size(), &b));
  CHECK(b.display_portrait == 1);
}

TEST_CASE("a v6 config blob defaults the beat style to number") {
  neon::Config a;
  a.beat_style = neon::BeatStyle::kPulse;

  std::vector<uint8_t> full(neon::config_blob_size());
  REQUIRE(neon::config_encode(a, full.data(), full.size()) == full.size());

  struct Hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t crc;
  };
  Hdr h;
  std::memcpy(&h, full.data(), sizeof(h));
  h.version = 6;
  h.crc = neon::crc32(full.data() + sizeof(h), h.payload_size);
  std::memcpy(full.data(), &h, sizeof(h));

  neon::Config b;
  REQUIRE(neon::config_decode(full.data(), full.size(), &b));
  CHECK(b.beat_style == neon::BeatStyle::kNumber);
}

TEST_CASE("audio values out of range are clamped") {
  neon::Config cfg;
  cfg.audio.role_l = static_cast<neon::AudioRole>(99);
  cfg.audio.role_r = static_cast<neon::AudioRole>(8);
  cfg.audio.metro_sound = static_cast<neon::ClickSound>(7);
  cfg.audio.amy_patch = 200;
  cfg.audio.la_jitter_ms = 5000;
  cfg.audio.enabled = 200;
  neon::config_sanitize(&cfg);
  CHECK(cfg.audio.role_l == neon::AudioRole::kMix);
  CHECK(cfg.audio.role_r == neon::AudioRole::kMix);
  CHECK(cfg.audio.metro_sound == neon::ClickSound::kSine);
  CHECK(cfg.audio.amy_patch == 0);
  CHECK(cfg.audio.la_jitter_ms == 800);
  CHECK(cfg.audio.enabled == 1);

  cfg.audio.la_jitter_ms = 1;
  neon::config_sanitize(&cfg);
  CHECK(cfg.audio.la_jitter_ms == 5);
}

TEST_CASE("audio_engine_config carries only the live-applied fields") {
  neon::Config cfg;
  cfg.quantum_beats = 3;
  cfg.audio.enabled = 1;
  cfg.audio.metro_enabled = 1;
  cfg.audio.metro_gain = 210;
  cfg.audio.role_r = neon::AudioRole::kReset;
  cfg.audio.la_jitter_ms = 90;
  cfg.audio.la_fullband = 1;
  const neon::AudioEngineConfig ec = neon::audio_engine_config(cfg);
  CHECK(ec.enabled == 1);
  CHECK(ec.metro_enabled == 1);
  CHECK(ec.metro_gain == 210);
  CHECK(ec.role_r == neon::AudioRole::kReset);
  CHECK(ec.la_jitter_ms == 90);
  CHECK(ec.la_fullband == 1);
  CHECK(ec.i2s_needed == 1);
  CHECK(ec.quantum_beats == 3);
}

TEST_CASE("i2s_needed follows engine on or Link Audio pub/sub") {
  neon::Config cfg;
  CHECK(neon::audio_engine_config(cfg).i2s_needed == 0);
  cfg.audio.la_publish_mix = 1;
  CHECK(neon::audio_engine_config(cfg).i2s_needed == 1);
  cfg.audio.la_publish_mix = 0;
  std::strcpy(cfg.audio.la_sub_channel_id, "peer-out");
  CHECK(neon::audio_engine_config(cfg).i2s_needed == 1);
}

TEST_CASE("published channel names derive from the device name") {
  neon::Config cfg;
  std::strcpy(cfg.device_name, "neon-link");
  char name[64] = {};
  neon::audio_channel_name(cfg, /*line_in=*/false, name, sizeof(name));
  CHECK(std::string(name) == "neon-link Out");
  neon::audio_channel_name(cfg, /*line_in=*/true, name, sizeof(name));
  CHECK(std::string(name) == "neon-link In");

  std::strcpy(cfg.audio.la_channel_name, "Tour Bus");
  neon::audio_channel_name(cfg, false, name, sizeof(name));
  CHECK(std::string(name) == "Tour Bus Out");
}
