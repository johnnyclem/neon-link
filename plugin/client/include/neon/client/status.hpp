#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace neon::client {

enum class NetworkKind { Ethernet, Wifi, None };
enum class SubState { Idle, Buffering, Playing };

struct PulseStatus {
  uint32_t edges = 0;
  uint32_t late_max_us = 0;
  uint32_t late_avg_us = 0;
};

struct AudioStatus {
  bool running = false;
  uint32_t underruns = 0;
  uint32_t peak_l = 0;
  uint32_t peak_r = 0;
  bool publishing = false;
  uint32_t subscribers = 0;
  SubState sub_state = SubState::Idle;
  uint32_t sub_rate = 0;
  uint32_t sub_dropped = 0;
  uint32_t fill_ms = 0;
  int32_t clock_ppm = 0;
  int32_t clock_residual_us = 0;
  uint32_t rx_dropped = 0;
  uint32_t jit_underruns = 0;
  uint32_t tx_dropped = 0;
  int32_t trim_ppm = 0;
};

struct Status {
  double bpm = 0;
  uint32_t peers = 0;
  bool playing = false;
  NetworkKind network = NetworkKind::None;
  bool ext_clock = false;
  // "none" | "clk" | "midi" | "audio". Empty when the firmware omits it.
  std::string follow_source;
  int64_t uptime_s = 0;
  uint32_t phase_milli = 0;
  uint32_t quantum = 4;
  bool tempo_valid = false;
  std::string hostname;
  std::string device_name;
  std::string ip;
  bool setup_ap = false;
  std::string ap_ssid;
  std::string wifi_ssid;
  uint32_t wifi_pass_len = 0;
  uint32_t wifi_fail_reason = 0;
  std::string firmware;
  uint32_t rev = 0;
  bool persist_lazy = false;
  double set_bpm = 0;
  PulseStatus pulse;
  AudioStatus audio;
};

struct ScanResult {
  std::string ssid;
  int rssi = 0;
  bool open = false;
};

struct AudioChannel {
  std::string id;
  std::string name;
  std::string peer;
  uint32_t rate = 0;
  uint32_t channels = 0;
  bool local = false;
};

struct AudioChannels {
  bool available = false;
  std::vector<AudioChannel> channels;
};

// Write-only secrets are never in GET / PUT echoes — only whether they exist.
struct ConfigSecrets {
  bool wifi_has_pass[4] = {};
  bool ap_has_pass = false;
};

// Returns false on malformed JSON. Missing fields keep their defaults:
// persist_lazy absent → false, rev absent → 0.
bool parse_status(const char* json, size_t len, Status* out);
bool parse_scan(const char* json, size_t len, std::vector<ScanResult>* out);
bool parse_audio_channels(const char* json, size_t len, AudioChannels* out);
bool parse_config_secrets(const char* json, size_t len, ConfigSecrets* out);

}  // namespace neon::client
