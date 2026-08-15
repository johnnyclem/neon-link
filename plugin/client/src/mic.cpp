#include "neon/client/mic.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "cJSON.h"

namespace neon::client {
namespace {

const cJSON* obj(const cJSON* o, const char* key) {
  return cJSON_GetObjectItemCaseSensitive(o, key);
}

bool get_bool(const cJSON* o, const char* key, bool* out) {
  const cJSON* v = obj(o, key);
  if (cJSON_IsBool(v)) {
    *out = cJSON_IsTrue(v);
    return true;
  }
  if (cJSON_IsNumber(v)) {
    *out = v->valuedouble != 0;
    return true;
  }
  return false;
}

bool get_num(const cJSON* o, const char* key, double* out) {
  const cJSON* v = obj(o, key);
  if (!cJSON_IsNumber(v)) {
    return false;
  }
  *out = v->valuedouble;
  return true;
}

bool get_str(const cJSON* o, const char* key, std::string* out) {
  const cJSON* v = obj(o, key);
  if (!cJSON_IsString(v) || v->valuestring == nullptr) {
    return false;
  }
  *out = v->valuestring;
  return true;
}

double clampd(double v, double lo, double hi) {
  if (!std::isfinite(v)) {
    return lo;
  }
  return std::min(hi, std::max(lo, v));
}

int clampi(double v, int lo, int hi) {
  if (!std::isfinite(v)) {
    return lo;
  }
  const int n = static_cast<int>(std::lround(v));
  return std::min(hi, std::max(lo, n));
}

std::string clip_str(std::string s, size_t max) {
  if (s.size() > max) {
    s.resize(max);
  }
  return s;
}

std::string trim_copy(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

void apply_config_fields(const cJSON* root, MicConfig* cfg) {
  double n = 0;
  if (get_num(root, "config_version", &n)) {
    cfg->config_version = std::max(1, clampi(n, 1, 99));
  }
  if (get_num(root, "rev", &n)) {
    cfg->rev = static_cast<uint32_t>(clampi(n, 0, 1000000000));
  }
  std::string s;
  if (get_str(root, "peer_name", &s)) {
    cfg->peer_name = clip_str(trim_copy(std::move(s)), kMicPeerNameMax);
    if (cfg->peer_name.empty()) {
      cfg->peer_name = "iPhone";
    }
  }
  if (get_num(root, "gain_db", &n)) {
    cfg->gain_db = clampd(n, -24, 36);
  }
  if (get_str(root, "source_id", &s)) {
    cfg->source_id = clip_str(std::move(s), 256);
  }
  bool b = false;
  if (get_bool(root, "keep_alive", &b)) {
    cfg->keep_alive = b;
  }
  if (get_bool(root, "start_stop_sync", &b)) {
    cfg->start_stop_sync = b;
  }
  if (get_bool(root, "publish", &b)) {
    cfg->publish = b;
  }
  if (get_num(root, "jitter_ms", &n)) {
    cfg->jitter_ms = clampi(n, 10, 500);
  }
  if (get_bool(root, "metronome_enabled", &b)) {
    cfg->metronome_enabled = b;
  }
  if (get_num(root, "metronome_gain_db", &n)) {
    cfg->metronome_gain_db = clampd(n, -60, 6);
  }
  if (get_str(root, "metronome_sound", &s)) {
    cfg->metronome_sound = metronome_sound_from(s.c_str());
  }
  if (get_bool(root, "metronome_accent", &b)) {
    cfg->metronome_accent = b;
  }
  if (get_num(root, "monitor_gain_db", &n)) {
    cfg->monitor_gain_db = clampd(n, -60, 0);
  }
  cfg->kind = kMicKind;
}

void apply_status_fields(const cJSON* root, MicStatus* st) {
  std::string s;
  if (get_str(root, "device_name", &s)) {
    st->device_name = clip_str(std::move(s), 47);
    if (st->device_name.empty()) {
      st->device_name = "neon-mic";
    }
  }
  if (get_str(root, "hostname", &s)) {
    st->hostname = clip_str(std::move(s), 63);
    if (st->hostname.empty()) {
      st->hostname = kMicDefaultHost;
    }
  }
  if (get_str(root, "ip", &s)) {
    st->ip = clip_str(std::move(s), 45);
  }
  if (get_str(root, "firmware", &s)) {
    st->firmware = clip_str(std::move(s), 31);
    if (st->firmware.empty()) {
      st->firmware = "0.1.0";
    }
  }
  double n = 0;
  if (get_num(root, "rev", &n)) {
    st->rev = static_cast<uint32_t>(clampi(n, 0, 1000000000));
  }
  if (get_str(root, "presence", &s)) {
    st->presence = (s == "online") ? MicPresence::Online : MicPresence::Offline;
  }
  bool b = false;
  if (get_bool(root, "streaming", &b)) {
    st->streaming = b;
  }
  if (get_bool(root, "on_session", &b)) {
    st->on_session = b;
  }
  if (get_str(root, "permission", &s)) {
    if (s == "granted") {
      st->permission = MicPermission::Granted;
    } else if (s == "denied") {
      st->permission = MicPermission::Denied;
    } else {
      st->permission = MicPermission::Unknown;
    }
  }
  if (get_bool(root, "playing", &b)) {
    st->playing = b;
  }
  if (get_num(root, "bpm", &n)) {
    st->bpm = clampd(n, 0, 400);
  }
  if (get_num(root, "phase_milli", &n)) {
    st->phase_milli = static_cast<uint32_t>(clampi(n, 0, 64000));
  }
  if (get_num(root, "quantum", &n)) {
    st->quantum = static_cast<uint32_t>(clampi(n, 1, 16));
  }
  if (get_num(root, "peers", &n)) {
    st->peers = static_cast<uint32_t>(clampi(n, 0, 256));
  }
  if (get_num(root, "subscribers", &n)) {
    st->subscribers = static_cast<uint32_t>(clampi(n, 0, 256));
  }
  if (get_num(root, "sample_rate", &n)) {
    st->sample_rate = static_cast<uint32_t>(clampi(n, 8000, 192000));
  }
  if (get_num(root, "latency_ms", &n)) {
    st->latency_ms = clampd(n, 0, 500);
  }
  if (get_num(root, "rms", &n)) {
    st->rms = clampd(n, 0, 1);
  }
  if (get_num(root, "peak", &n)) {
    st->peak = clampd(n, 0, 1);
  }
  if (get_bool(root, "clip", &b)) {
    st->clip = b;
  }
  if (get_str(root, "source_label", &s)) {
    st->source_label = clip_str(std::move(s), 64);
  }
  const cJSON* err = obj(root, "error");
  if (cJSON_IsString(err) && err->valuestring != nullptr && err->valuestring[0] != '\0') {
    st->error = clip_str(err->valuestring, 240);
  } else {
    st->error.clear();
  }
  if (get_num(root, "uptime_s", &n)) {
    st->uptime_s = static_cast<uint32_t>(clampi(n, 0, 1000000000));
  }
  st->kind = kMicKind;
}

DocumentKind kind_from_value(const cJSON* v) {
  if (v == nullptr || cJSON_IsNull(v)) {
    return DocumentKind::NeonLink;
  }
  if (!cJSON_IsString(v) || v->valuestring == nullptr || v->valuestring[0] == '\0') {
    return DocumentKind::NeonLink;
  }
  const char* k = v->valuestring;
  if (std::strcmp(k, "phone-mic") == 0) {
    return DocumentKind::PhoneMic;
  }
  if (std::strcmp(k, "neon-interface") == 0) {
    return DocumentKind::NeonInterface;
  }
  if (std::strcmp(k, "neon-link") == 0) {
    return DocumentKind::NeonLink;
  }
  return DocumentKind::Unknown;
}

}  // namespace

const char* metronome_sound_name(MetronomeSound s) {
  switch (s) {
    case MetronomeSound::Noise:
      return "noise";
    case MetronomeSound::Wood:
      return "wood";
    case MetronomeSound::Sine:
    default:
      return "sine";
  }
}

MetronomeSound metronome_sound_from(const char* name) {
  if (name != nullptr) {
    if (std::strcmp(name, "noise") == 0) {
      return MetronomeSound::Noise;
    }
    if (std::strcmp(name, "wood") == 0) {
      return MetronomeSound::Wood;
    }
  }
  return MetronomeSound::Sine;
}

MicConfig default_mic_config() {
  MicConfig c;
  sanitize_mic_config(&c);
  return c;
}

MicStatus default_mic_status() {
  MicStatus s;
  sanitize_mic_status(&s);
  return s;
}

void sanitize_mic_config(MicConfig* cfg) {
  if (cfg == nullptr) {
    return;
  }
  cfg->kind = kMicKind;
  cfg->config_version = std::max(1, cfg->config_version);
  cfg->peer_name = clip_str(trim_copy(cfg->peer_name), kMicPeerNameMax);
  if (cfg->peer_name.empty()) {
    cfg->peer_name = "iPhone";
  }
  cfg->gain_db = clampd(cfg->gain_db, -24, 36);
  if (cfg->source_id.size() > 256) {
    cfg->source_id.resize(256);
  }
  cfg->jitter_ms = std::min(500, std::max(10, cfg->jitter_ms));
  cfg->metronome_gain_db = clampd(cfg->metronome_gain_db, -60, 6);
  cfg->monitor_gain_db = clampd(cfg->monitor_gain_db, -60, 0);
}

void sanitize_mic_status(MicStatus* st) {
  if (st == nullptr) {
    return;
  }
  st->kind = kMicKind;
  if (st->device_name.empty()) {
    st->device_name = "neon-mic";
  }
  if (st->hostname.empty()) {
    st->hostname = kMicDefaultHost;
  }
  if (st->firmware.empty()) {
    st->firmware = "0.1.0";
  }
  const bool live = st->presence == MicPresence::Online && st->streaming && st->on_session;
  st->streaming = st->presence == MicPresence::Online && st->streaming;
  st->on_session = live;
}

DocumentKind probe_document_kind(const char* json, size_t len) {
  if (json == nullptr) {
    return DocumentKind::Unknown;
  }
  cJSON* root = cJSON_ParseWithLength(json, len);
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return DocumentKind::Unknown;
  }
  const DocumentKind k = kind_from_value(obj(root, "kind"));
  cJSON_Delete(root);
  return k;
}

bool parse_mic_status(const char* json, size_t len, MicStatus* out) {
  if (json == nullptr || out == nullptr) {
    return false;
  }
  cJSON* root = cJSON_ParseWithLength(json, len);
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return false;
  }
  if (kind_from_value(obj(root, "kind")) != DocumentKind::PhoneMic) {
    cJSON_Delete(root);
    return false;
  }
  MicStatus st = default_mic_status();
  apply_status_fields(root, &st);
  sanitize_mic_status(&st);
  cJSON_Delete(root);
  *out = std::move(st);
  return true;
}

bool parse_mic_config(const char* json, size_t len, MicConfig* out) {
  if (json == nullptr || out == nullptr) {
    return false;
  }
  cJSON* root = cJSON_ParseWithLength(json, len);
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return false;
  }
  if (kind_from_value(obj(root, "kind")) != DocumentKind::PhoneMic) {
    cJSON_Delete(root);
    return false;
  }
  MicConfig cfg = default_mic_config();
  apply_config_fields(root, &cfg);
  sanitize_mic_config(&cfg);
  cJSON_Delete(root);
  *out = std::move(cfg);
  return true;
}

bool parse_mic_sources(const char* json, size_t len, std::vector<MicSourceRow>* out) {
  if (json == nullptr || out == nullptr) {
    return false;
  }
  cJSON* root = cJSON_ParseWithLength(json, len);
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return false;
  }
  const cJSON* arr = obj(root, "sources");
  if (!cJSON_IsArray(arr)) {
    cJSON_Delete(root);
    return false;
  }
  std::vector<MicSourceRow> rows;
  const cJSON* it = nullptr;
  cJSON_ArrayForEach(it, arr) {
    if (!cJSON_IsObject(it)) {
      continue;
    }
    std::string id;
    if (!get_str(it, "id", &id)) {
      continue;
    }
    MicSourceRow row;
    row.id = clip_str(std::move(id), 256);
    std::string label;
    if (get_str(it, "label", &label)) {
      row.label = clip_str(std::move(label), 64);
    }
    rows.push_back(std::move(row));
  }
  cJSON_Delete(root);
  *out = std::move(rows);
  return true;
}

bool parse_capture_reply(const char* json, size_t len, CaptureReply* out) {
  if (json == nullptr || out == nullptr) {
    return false;
  }
  cJSON* root = cJSON_ParseWithLength(json, len);
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return false;
  }
  CaptureReply r;
  bool b = false;
  if (get_bool(root, "ok", &b)) {
    r.ok = b;
  }
  if (get_bool(root, "streaming", &b)) {
    r.streaming = b;
  }
  cJSON_Delete(root);
  *out = r;
  return true;
}

std::string mic_config_patch_json(const MicConfig& from, const MicConfig& to) {
  cJSON* o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "kind", kMicKind);
  if (from.peer_name != to.peer_name) {
    cJSON_AddStringToObject(o, "peer_name", to.peer_name.c_str());
  }
  if (from.gain_db != to.gain_db) {
    cJSON_AddNumberToObject(o, "gain_db", to.gain_db);
  }
  if (from.source_id != to.source_id) {
    cJSON_AddStringToObject(o, "source_id", to.source_id.c_str());
  }
  if (from.keep_alive != to.keep_alive) {
    cJSON_AddBoolToObject(o, "keep_alive", to.keep_alive);
  }
  if (from.start_stop_sync != to.start_stop_sync) {
    cJSON_AddBoolToObject(o, "start_stop_sync", to.start_stop_sync);
  }
  if (from.publish != to.publish) {
    cJSON_AddBoolToObject(o, "publish", to.publish);
  }
  if (from.jitter_ms != to.jitter_ms) {
    cJSON_AddNumberToObject(o, "jitter_ms", to.jitter_ms);
  }
  if (from.metronome_enabled != to.metronome_enabled) {
    cJSON_AddBoolToObject(o, "metronome_enabled", to.metronome_enabled);
  }
  if (from.metronome_gain_db != to.metronome_gain_db) {
    cJSON_AddNumberToObject(o, "metronome_gain_db", to.metronome_gain_db);
  }
  if (from.metronome_sound != to.metronome_sound) {
    cJSON_AddStringToObject(o, "metronome_sound",
                            metronome_sound_name(to.metronome_sound));
  }
  if (from.metronome_accent != to.metronome_accent) {
    cJSON_AddBoolToObject(o, "metronome_accent", to.metronome_accent);
  }
  if (from.monitor_gain_db != to.monitor_gain_db) {
    cJSON_AddNumberToObject(o, "monitor_gain_db", to.monitor_gain_db);
  }
  if (from.config_version != to.config_version) {
    cJSON_AddNumberToObject(o, "config_version", to.config_version);
  }
  char* printed = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if (printed == nullptr) {
    return "{\"kind\":\"phone-mic\"}";
  }
  std::string out(printed);
  cJSON_free(printed);
  return out;
}

MicConfig merge_mic_config(const MicConfig& base, const char* json, size_t len) {
  MicConfig cfg = base;
  if (json == nullptr) {
    sanitize_mic_config(&cfg);
    return cfg;
  }
  cJSON* root = cJSON_ParseWithLength(json, len);
  if (root != nullptr && cJSON_IsObject(root)) {
    apply_config_fields(root, &cfg);
    cfg.rev = base.rev;
  }
  cJSON_Delete(root);
  sanitize_mic_config(&cfg);
  return cfg;
}

MicHost parse_mic_host(std::string_view input) {
  std::string t = trim_copy(std::string(input));
  if (t.empty()) {
    return {kMicDefaultHost, kMicDefaultPort};
  }
  if (iequals(t.substr(0, 8), "https://")) {
    t.erase(0, 8);
  } else if (iequals(t.substr(0, 7), "http://")) {
    t.erase(0, 7);
  }
  while (!t.empty() && t.back() == '/') {
    t.pop_back();
  }
  const auto slash = t.find('/');
  if (slash != std::string::npos) {
    t.resize(slash);
  }
  if (t.empty()) {
    return {kMicDefaultHost, kMicDefaultPort};
  }

  if (t.front() == '[') {
    const auto close = t.find(']');
    if (close != std::string::npos) {
      MicHost h;
      h.host = t.substr(1, close - 1);
      if (close + 1 < t.size() && t[close + 1] == ':') {
        const int p = std::atoi(t.c_str() + close + 2);
        h.port = (p > 0 && p <= 65535) ? p : kMicDefaultPort;
      }
      if (h.host.empty()) {
        h.host = kMicDefaultHost;
      }
      return h;
    }
  }

  const auto colon = t.rfind(':');
  if (colon != std::string::npos && colon > 0) {
    const std::string rest = t.substr(colon + 1);
    bool digits = !rest.empty();
    for (char c : rest) {
      if (c < '0' || c > '9') {
        digits = false;
        break;
      }
    }
    if (digits) {
      const int p = std::atoi(rest.c_str());
      MicHost h;
      h.host = t.substr(0, colon);
      h.port = (p > 0 && p <= 65535) ? p : kMicDefaultPort;
      return h;
    }
  }
  return {t, kMicDefaultPort};
}

PresenceWord presence_word(const MicStatus& st) {
  if (st.presence != MicPresence::Online || !st.streaming) {
    return PresenceWord::Idle;
  }
  return st.on_session ? PresenceWord::Live : PresenceWord::Local;
}

}  // namespace neon::client
