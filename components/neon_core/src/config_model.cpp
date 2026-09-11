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
      cfg->clock_source != ClockSource::kExternalMaster &&
      cfg->clock_source != ClockSource::kMidiMaster) {
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
  // WPA2 needs 8 characters. Fail closed: restore the default key rather
  // than silently opening the network (an open AP exposes the whole
  // unauthenticated /api surface, OTA included, to anyone in RF range).
  if (cfg->ap_require_pass && std::strlen(cfg->ap_pass) < 8) {
    std::memcpy(cfg->ap_pass, kDefaultApPass, sizeof(kDefaultApPass));
  }

  cfg->device_name[sizeof(cfg->device_name) - 1] = '\0';
  char host[sizeof(cfg->device_name)] = {};
  sanitize_hostname(cfg->device_name, host, sizeof(host));
  std::memcpy(cfg->device_name, host, sizeof(host));

  cfg->big_beat_display = cfg->big_beat_display ? 1 : 0;
  if (cfg->beat_style >= BeatStyle::kCount) {
    cfg->beat_style = BeatStyle::kNumber;
  }
  if (cfg->color_theme >= ColorTheme::kCount) {
    cfg->color_theme = ColorTheme::kLink;
  }
  cfg->midi_trs_type = cfg->midi_trs_type ? 1 : 0;
  if (cfg->display_dim_s > 3600) {
    cfg->display_dim_s = 3600;
  }
  cfg->osc_enabled = cfg->osc_enabled ? 1 : 0;
  if (cfg->osc_port == 0) {
    cfg->osc_port = 9000;
  }
  cfg->osc_target[sizeof(cfg->osc_target) - 1] = '\0';
  cfg->display_portrait = cfg->display_portrait ? 1 : 0;
  if (cfg->mono_theme >= MonoTheme::kCount) {
    cfg->mono_theme = MonoTheme::kClassic;
  }

  AudioConfig& a = cfg->audio;
  a.enabled = a.enabled ? 1 : 0;
  if (a.role_l >= AudioRole::kRoleCount) {
    a.role_l = AudioRole::kMix;
  }
  if (a.role_r >= AudioRole::kRoleCount) {
    a.role_r = AudioRole::kMix;
  }
  a.metro_enabled = a.metro_enabled ? 1 : 0;
  // A zero gain with the click armed is how LINE OUT went dead after a
  // VST save: empty NumberField commits wrote 0. Mute is the toggle.
  if (a.metro_enabled && a.metro_gain == 0) {
    a.metro_gain = kUnityGainByte;
  }
  if (a.metro_sound >= ClickSound::kSoundCount) {
    a.metro_sound = ClickSound::kSine;
  }
  a.metro_accent = a.metro_accent ? 1 : 0;
  a.amy_enabled = a.amy_enabled ? 1 : 0;
  a.amy_patch = static_cast<uint8_t>(a.amy_patch % 4);
  a.la_publish_mix = a.la_publish_mix ? 1 : 0;
  a.la_publish_linein = a.la_publish_linein ? 1 : 0;
  a.la_publish_mono = a.la_publish_mono ? 1 : 0;
  a.la_fullband = a.la_fullband ? 1 : 0;
  clamp<uint16_t>(&a.la_jitter_ms, 5, 800);
  a.la_channel_name[sizeof(a.la_channel_name) - 1] = '\0';
  a.la_sub_channel_id[sizeof(a.la_sub_channel_id) - 1] = '\0';

  if (cfg->priority_profile != PriorityProfile::kLegacy) {
    cfg->priority_profile = PriorityProfile::kFixed;
  }
  cfg->telemetry_uart_csv = cfg->telemetry_uart_csv ? 1 : 0;

  cfg->device_token[sizeof(cfg->device_token) - 1] = '\0';
}

int link_asio_task_priority(PriorityProfile profile) {
  // 12: above the timeline poll (10) and the Link Audio pump (9) — see
  // link_overrides/ableton/platforms/esp32/Context.hpp. 8 recreates the
  // inversion the fix corrected (docs/STUDIO_MODE_TEST_PLAN.md Phase 2).
  return profile == PriorityProfile::kLegacy ? 8 : 12;
}

int link_pump_task_priority(PriorityProfile profile) {
  // 9: below the asio service task and the timeline poll — see
  // components/ableton_link/src/link_audio_esp.cpp. 11 recreates the
  // inversion the fix corrected.
  return profile == PriorityProfile::kLegacy ? 11 : 9;
}

const char* priority_profile_str(PriorityProfile profile) {
  return profile == PriorityProfile::kLegacy ? "legacy" : "fixed";
}

AudioEngineConfig audio_engine_config(const Config& cfg) {
  const AudioConfig& a = cfg.audio;
  AudioEngineConfig out;
  out.enabled = a.enabled;
  out.role_l = a.role_l;
  out.role_r = a.role_r;
  out.metro_enabled = a.metro_enabled;
  out.metro_sound = a.metro_sound;
  out.metro_gain = a.metro_gain;
  out.metro_accent = a.metro_accent;
  out.amy_enabled = a.amy_enabled;
  out.amy_gain = a.amy_gain;
  out.amy_patch = a.amy_patch;
  out.linein_monitor_gain = a.linein_monitor_gain;
  out.la_sub_gain = a.la_sub_gain;
  out.la_publish_mono = a.la_publish_mono;
  out.la_fullband = a.la_fullband;
  out.la_jitter_ms = a.la_jitter_ms;
  out.i2s_needed =
      (a.enabled != 0 || a.la_publish_mix != 0 || a.la_publish_linein != 0 ||
       a.la_sub_channel_id[0] != '\0')
          ? 1
          : 0;
  out.quantum_beats = cfg.quantum_beats;
  out.priority_profile = static_cast<uint8_t>(cfg.priority_profile);
  return out;
}

size_t audio_channel_name(const Config& cfg, bool line_in, char* out,
                          size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const char* base = cfg.audio.la_channel_name[0] != '\0'
                         ? cfg.audio.la_channel_name
                         : cfg.device_name;
  std::snprintf(out, cap, "%s %s", base, line_in ? "In" : "Out");
  return std::strlen(out);
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

bool network_identity_changed(const Config& a, const Config& b) {
  if (a.ap_policy != b.ap_policy || a.ap_require_pass != b.ap_require_pass ||
      a.ap_hidden != b.ap_hidden || a.ap_channel != b.ap_channel) {
    return true;
  }
  if (std::strcmp(a.ap_ssid, b.ap_ssid) != 0 ||
      std::strcmp(a.ap_pass, b.ap_pass) != 0) {
    return true;
  }
  for (int i = 0; i < kWifiSlots; ++i) {
    if (std::strcmp(a.wifi[i].ssid, b.wifi[i].ssid) != 0 ||
        std::strcmp(a.wifi[i].pass, b.wifi[i].pass) != 0 ||
        a.wifi[i].hidden != b.wifi[i].hidden) {
      return true;
    }
  }
  return false;
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

size_t derive_ap_pass_from_mac(const uint8_t mac[6], char* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  // "link-" plus 6 hex digits from the last 3 MAC bytes: 11 characters,
  // comfortably past WPA2's 8-character floor, and in the same
  // "<name>-XXXX" family as ap_ssid_for()'s derived SSID so the two read
  // as one convention on the OLED setup screen.
  const uint8_t a = mac != nullptr ? mac[3] : 0;
  const uint8_t b = mac != nullptr ? mac[4] : 0;
  const uint8_t c = mac != nullptr ? mac[5] : 0;
  std::snprintf(out, cap, "link-%02X%02X%02X", a, b, c);
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
  if (out == nullptr || len < sizeof(BlobHeader)) {
    return false;
  }
  BlobHeader h;
  std::memcpy(&h, buf, sizeof(BlobHeader));
  // Older versions are a prefix of Config. Reject anything newer or
  // larger than we know how to read; smaller payloads keep defaults
  // for fields that did not exist yet (v2 → big_beat_display = on).
  if (h.magic != kConfigMagic || h.version == 0 ||
      h.version > kConfigVersion || h.payload_size == 0 ||
      h.payload_size > sizeof(Config) ||
      len < sizeof(BlobHeader) + h.payload_size) {
    return false;
  }
  if (crc32(buf + sizeof(BlobHeader), h.payload_size) != h.crc) {
    return false;
  }
  *out = Config{};
  std::memcpy(out, buf + sizeof(BlobHeader), h.payload_size);
  // v2's sizeof included tail padding after ap_pass. That padding lands
  // on big_beat_display and would silently turn the new default off.
  if (h.version < 3) {
    out->big_beat_display = 1;
  }
  if (h.version < 4) {
    // Same trap one version on: a v3 payload's size ran past
    // big_beat_display into its own tail padding, and that padding would
    // land on the audio block. None of it is configuration.
    out->audio = AudioConfig{};
  }
  if (h.version < 6) {
    // Same trap again: device_token is new tail after telemetry_uart_csv,
    // so a v5 payload's trailing alignment padding lands on its first
    // bytes. None of it is a real token — a stale/garbage token would
    // just lock the owner out of their own OTA and factory-reset until a
    // web UI that happens to send it, which never existed, connects.
    std::memset(out->device_token, 0, sizeof(out->device_token));
  }
  if (h.version < 7) {
    // beat_style sits in what was v6 tail padding after device_token.
    out->beat_style = BeatStyle::kNumber;
  }
  if (h.version < 8) {
    out->midi_trs_type = 0;
  }
  if (h.version < 9) {
    // color_theme sits in what was v8 tail padding after midi_trs_type.
    out->color_theme = ColorTheme::kTeal;
  }
  if (h.version < 10) {
    // display_dim_s/_level sit in what was v9 tail padding after
    // color_theme. Dimming stays off until the user turns it on.
    out->display_dim_s = 0;
    out->display_dim_level = 64;
  }
  if (h.version < 11) {
    // The OSC block is new tail after display_dim_level; stale padding
    // must not enable a network control surface.
    out->osc_enabled = 0;
    out->osc_port = 9000;
    std::memset(out->osc_target, 0, sizeof(out->osc_target));
  }
  if (h.version < 12) {
    // display_portrait sits in what was v11 tail padding after osc_target.
    // Default landscape so existing units keep their current orientation.
    out->display_portrait = 0;
  }
  if (h.version < 13) {
    // mono_theme sits in what was v12 tail padding after display_portrait.
    out->mono_theme = MonoTheme::kClassic;
  }
  if (h.version < 14) {
    // Factory colour default was Teal. Link (graphite + orange) is the
    // neon-link face; remap only Teal so a user who picked Amber/Void
    // keeps it.
    if (out->color_theme == ColorTheme::kTeal) {
      out->color_theme = ColorTheme::kLink;
    }
  }
  config_sanitize(out);
  return true;
}

}  // namespace neon
