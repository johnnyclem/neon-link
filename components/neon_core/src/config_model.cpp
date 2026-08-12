#include "neon/config/model.hpp"

#include <cstdio>
#include <cstring>
#include <type_traits>

namespace neon {

namespace {

struct BlobHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t payload_size;
  uint32_t crc;
};
static_assert(sizeof(BlobHeader) == 12, "packed header expected");

// encode/decode memcpy the struct wholesale, and EngineConfig rides a
// SeqLock to core 1 — both require a trivially copyable payload.
static_assert(std::is_trivially_copyable<Config>::value,
              "Config must stay trivially copyable");
static_assert(std::is_trivially_copyable<EngineConfig>::value,
              "EngineConfig must stay trivially copyable");

template <typename T>
void clamp(T* v, T lo, T hi) {
  if (*v < lo) *v = lo;
  if (*v > hi) *v = hi;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc >> 1) ^ (0xedb88320u & (~(crc & 1u) + 1u));
    }
  }
  return ~crc;
}

void config_sanitize(Config* cfg) {
  for (auto& c : cfg->engine.clocks) {
    clamp<uint32_t>(&c.ppqn, 1, 192);
    clamp<uint32_t>(&c.mult, 1, 16);
    clamp<uint32_t>(&c.div, 1, 16);
    clamp<uint32_t>(&c.trig_len_us, 100, 100000);
    clamp<uint8_t>(&c.duty_pct, 1, 99);
    clamp<uint8_t>(&c.shuffle_pct, 0, 75);
    if (c.mode != ClockOutputConfig::PulseMode::kSquare) {
      c.mode = ClockOutputConfig::PulseMode::kTrigger;
    }
    if (c.rhythm != ClockOutputConfig::RhythmMode::kEuclid &&
        c.rhythm != ClockOutputConfig::RhythmMode::kProbability &&
        c.rhythm != ClockOutputConfig::RhythmMode::kPattern) {
      c.rhythm = ClockOutputConfig::RhythmMode::kAll;
    }
    if (c.role > OutputRole::kResetStop) {
      c.role = OutputRole::kClock;
    }
    clamp<uint8_t>(&c.euclid_steps, 1, 64);
    clamp<uint8_t>(&c.euclid_fills, 0, 64);
    clamp<uint8_t>(&c.euclid_rot, 0, 63);
    clamp<uint8_t>(&c.probability_pct, 0, 100);
    clamp<uint8_t>(&c.humanize_pct, 0, 50);
  }
  if (cfg->engine.reset_mode != ResetMode::kEveryBar &&
      cfg->engine.reset_mode != ResetMode::kOff &&
      cfg->engine.reset_mode != ResetMode::kAtStop) {
    cfg->engine.reset_mode = ResetMode::kStartOfPlay;
  }
  clamp<uint32_t>(&cfg->engine.reset_trig_len_us, 100, 100000);
  clamp<int32_t>(&cfg->engine.latency_us, -50000, 50000);
  clamp<uint32_t>(&cfg->engine.reset_lead_us, 0, 50000);
  clamp<uint32_t>(&cfg->engine.quantum_beats, 1, 16);
  clamp<uint16_t>(&cfg->tempo_cv_min_bpm, 1, 998);
  clamp<uint16_t>(&cfg->tempo_cv_max_bpm, 2, 999);
  if (cfg->tempo_cv_max_bpm <= cfg->tempo_cv_min_bpm) {
    cfg->tempo_cv_max_bpm = cfg->tempo_cv_min_bpm + 1;
  }
  clamp<uint32_t>(&cfg->quantum_beats, 1, 16);
  cfg->engine.quantum_beats = cfg->quantum_beats;
  if (cfg->clock_source != ClockSource::kLinkMaster &&
      cfg->clock_source != ClockSource::kExternalMaster) {
    cfg->clock_source = ClockSource::kAuto;
  }
  clamp<uint32_t>(&cfg->clock_in_ppqn, 1, 96);

  cfg->ble_enabled = cfg->ble_enabled ? 1 : 0;
  cfg->midi_clock_out = cfg->midi_clock_out ? 1 : 0;
  cfg->start_stop_sync = cfg->start_stop_sync ? 1 : 0;
  clamp<int32_t>(&cfg->midi_nudge_us, -100000, 100000);
  clamp<uint32_t>(&cfg->tempo_milli_bpm, kMinMilliBpm, kMaxMilliBpm);
  MidiRouteConfig& m = cfg->midi;
  if (m.midi_channel > 15) {
    m.midi_channel = 255;
  }
  if (m.gate_target > MidiRouteConfig::kTargetRun) {
    m.gate_target = MidiRouteConfig::kTargetNone;
  }
  if (m.cc_latency > 127) {
    m.cc_latency = MidiRouteConfig::kCcOff;
  }
  if (m.cc_shuffle_base > 124) {
    m.cc_shuffle_base = MidiRouteConfig::kCcOff;
  }
  if (m.clock_policy != MidiRouteConfig::ClockPolicy::kReplace &&
      m.clock_policy != MidiRouteConfig::ClockPolicy::kMerge) {
    m.clock_policy = MidiRouteConfig::ClockPolicy::kIgnore;
  }

  for (auto& n : cfg->wifi) {
    n.ssid[sizeof(n.ssid) - 1] = '\0';
    n.pass[sizeof(n.pass) - 1] = '\0';
    n.hidden = n.hidden ? 1 : 0;
    // A blank SSID means "slot unused"; never leave a stale password.
    if (n.ssid[0] == '\0') {
      n.pass[0] = '\0';
      n.hidden = 0;
    }
  }
  clamp<uint8_t>(&cfg->wifi_retries, 1, 10);

  if (cfg->ap_policy != ApPolicy::kAlways && cfg->ap_policy != ApPolicy::kOff) {
    cfg->ap_policy = ApPolicy::kFallback;
  }
  cfg->ap_require_pass = cfg->ap_require_pass ? 1 : 0;
  cfg->ap_hidden = cfg->ap_hidden ? 1 : 0;
  clamp<uint8_t>(&cfg->ap_channel, 1, 13);
  cfg->ap_ssid[sizeof(cfg->ap_ssid) - 1] = '\0';
  cfg->ap_pass[sizeof(cfg->ap_pass) - 1] = '\0';
  // WPA2 needs 8 characters; a shorter one would silently open the network.
  if (cfg->ap_require_pass && std::strlen(cfg->ap_pass) < 8) {
    cfg->ap_require_pass = 0;
  }

  cfg->device_name[sizeof(cfg->device_name) - 1] = '\0';
  char host[sizeof(cfg->device_name)] = {};
  sanitize_hostname(cfg->device_name, host, sizeof(host));
  std::memcpy(cfg->device_name, host, sizeof(host));
}

size_t sanitize_hostname(const char* in, char* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  size_t n = 0;
  bool last_dash = false;
  for (const char* p = in; p != nullptr && *p != '\0' && n + 1 < cap; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alnum) {
      out[n++] = c;
      last_dash = false;
    } else if (n != 0 && !last_dash) {
      out[n++] = '-';  // any separator run collapses to one hyphen
      last_dash = true;
    }
  }
  while (n != 0 && out[n - 1] == '-') {  // no trailing separator
    --n;
  }
  out[n] = '\0';
  if (n == 0) {
    const char* kFallback = "neon-link";
    for (const char* p = kFallback; *p != '\0' && n + 1 < cap; ++p) {
      out[n++] = *p;
    }
    out[n] = '\0';
  }
  return n;
}

size_t ap_ssid_for(const Config& cfg, const uint8_t mac[6], char* out,
                   size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  if (cfg.ap_ssid[0] != '\0') {
    std::snprintf(out, cap, "%s", cfg.ap_ssid);
    return std::strlen(out);
  }
  // Derived default, matching the "<NAME>-XXXX" convention users expect
  // from the serial-number sticker on legacy boxes.
  char upper[sizeof(cfg.device_name)] = {};
  size_t n = 0;
  for (const char* p = cfg.device_name;
       *p != '\0' && n + 1 < sizeof(upper); ++p) {
    char c = *p;
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
    upper[n++] = c;
  }
  upper[n] = '\0';
  const uint8_t a = mac != nullptr ? mac[4] : 0;
  const uint8_t b = mac != nullptr ? mac[5] : 0;
  std::snprintf(out, cap, "%s-%02X%02X", upper, a, b);
  return std::strlen(out);
}

size_t config_blob_size() { return sizeof(BlobHeader) + sizeof(Config); }

size_t config_encode(const Config& cfg, uint8_t* buf, size_t cap) {
  if (cap < config_blob_size()) {
    return 0;
  }
  BlobHeader h;
  h.magic = kConfigMagic;
  h.version = kConfigVersion;
  h.payload_size = static_cast<uint16_t>(sizeof(Config));
  std::memcpy(buf + sizeof(BlobHeader), &cfg, sizeof(Config));
  h.crc = crc32(buf + sizeof(BlobHeader), sizeof(Config));
  std::memcpy(buf, &h, sizeof(BlobHeader));
  return config_blob_size();
}

bool config_decode(const uint8_t* buf, size_t len, Config* out) {
  if (len < sizeof(BlobHeader)) {
    return false;
  }
  BlobHeader h;
  std::memcpy(&h, buf, sizeof(BlobHeader));
  if (h.magic != kConfigMagic || h.version != kConfigVersion ||
      h.payload_size != sizeof(Config) ||
      len < sizeof(BlobHeader) + h.payload_size) {
    return false;
  }
  if (crc32(buf + sizeof(BlobHeader), h.payload_size) != h.crc) {
    return false;
  }
  std::memcpy(out, buf + sizeof(BlobHeader), sizeof(Config));
  config_sanitize(out);
  return true;
}

}  // namespace neon
