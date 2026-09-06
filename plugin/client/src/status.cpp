#include "neon/client/status.hpp"

#include "cJSON.h"

namespace neon::client {
namespace {

const cJSON* obj(const cJSON* o, const char* key) {
  return cJSON_GetObjectItemCaseSensitive(o, key);
}

bool get_bool(const cJSON* o, const char* key, bool* out) {
  const cJSON* v = obj(o, key);
  if (!cJSON_IsBool(v)) {
    return false;
  }
  *out = cJSON_IsTrue(v);
  return true;
}

bool get_num(const cJSON* o, const char* key, double* out) {
  const cJSON* v = obj(o, key);
  if (!cJSON_IsNumber(v)) {
    return false;
  }
  *out = v->valuedouble;
  return true;
}

bool get_u32(const cJSON* o, const char* key, uint32_t* out) {
  double d = 0;
  if (!get_num(o, key, &d) || d < 0) {
    return false;
  }
  *out = static_cast<uint32_t>(d);
  return true;
}

bool get_i32(const cJSON* o, const char* key, int32_t* out) {
  double d = 0;
  if (!get_num(o, key, &d)) {
    return false;
  }
  *out = static_cast<int32_t>(d);
  return true;
}

bool get_i64(const cJSON* o, const char* key, int64_t* out) {
  double d = 0;
  if (!get_num(o, key, &d)) {
    return false;
  }
  *out = static_cast<int64_t>(d);
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

}  // namespace

bool parse_status(const char* json, size_t len, Status* out) {
  if (json == nullptr || out == nullptr) {
    return false;
  }
  cJSON* root = cJSON_ParseWithLength(json, len);
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return false;
  }
  Status s;
  get_num(root, "bpm", &s.bpm);
  get_u32(root, "peers", &s.peers);
  get_bool(root, "playing", &s.playing);
  std::string net;
  if (get_str(root, "network", &net)) {
    if (net == "ethernet") {
      s.network = NetworkKind::Ethernet;
    } else if (net == "wifi") {
      s.network = NetworkKind::Wifi;
    } else {
      s.network = NetworkKind::None;
    }
  }
  get_bool(root, "ext_clock", &s.ext_clock);
  get_str(root, "follow_source", &s.follow_source);
  get_i64(root, "uptime_s", &s.uptime_s);
  get_u32(root, "phase_milli", &s.phase_milli);
  get_u32(root, "quantum", &s.quantum);
  get_bool(root, "tempo_valid", &s.tempo_valid);
  get_str(root, "hostname", &s.hostname);
  get_str(root, "device_name", &s.device_name);
  get_str(root, "ip", &s.ip);
  get_bool(root, "setup_ap", &s.setup_ap);
  get_str(root, "ap_ssid", &s.ap_ssid);
  get_str(root, "wifi_ssid", &s.wifi_ssid);
  get_u32(root, "wifi_pass_len", &s.wifi_pass_len);
  get_u32(root, "wifi_fail_reason", &s.wifi_fail_reason);
  get_str(root, "firmware", &s.firmware);
  get_u32(root, "rev", &s.rev);
  get_bool(root, "persist_lazy", &s.persist_lazy);
  get_num(root, "set_bpm", &s.set_bpm);

  const cJSON* pulse = obj(root, "pulse");
  if (cJSON_IsObject(pulse)) {
    get_u32(pulse, "edges", &s.pulse.edges);
    get_u32(pulse, "late_max_us", &s.pulse.late_max_us);
    get_u32(pulse, "late_avg_us", &s.pulse.late_avg_us);
  }
  const cJSON* audio = obj(root, "audio");
  if (cJSON_IsObject(audio)) {
    get_bool(audio, "running", &s.audio.running);
    get_u32(audio, "underruns", &s.audio.underruns);
    get_u32(audio, "peak_l", &s.audio.peak_l);
    get_u32(audio, "peak_r", &s.audio.peak_r);
    get_bool(audio, "publishing", &s.audio.publishing);
    get_u32(audio, "subscribers", &s.audio.subscribers);
    std::string st;
    if (get_str(audio, "sub_state", &st)) {
      if (st == "playing") {
        s.audio.sub_state = SubState::Playing;
      } else if (st == "buffering") {
        s.audio.sub_state = SubState::Buffering;
      } else {
        s.audio.sub_state = SubState::Idle;
      }
    }
    get_u32(audio, "sub_rate", &s.audio.sub_rate);
    get_u32(audio, "sub_dropped", &s.audio.sub_dropped);
    get_u32(audio, "fill_ms", &s.audio.fill_ms);
    get_i32(audio, "clock_ppm", &s.audio.clock_ppm);
    get_i32(audio, "clock_residual_us", &s.audio.clock_residual_us);
    get_u32(audio, "rx_dropped", &s.audio.rx_dropped);
    get_u32(audio, "jit_underruns", &s.audio.jit_underruns);
    get_u32(audio, "tx_dropped", &s.audio.tx_dropped);
    get_i32(audio, "trim_ppm", &s.audio.trim_ppm);
  }

  cJSON_Delete(root);
  *out = std::move(s);
  return true;
}

bool parse_scan(const char* json, size_t len, std::vector<ScanResult>* out) {
  if (json == nullptr || out == nullptr) {
    return false;
  }
  cJSON* root = cJSON_ParseWithLength(json, len);
  if (root == nullptr || !cJSON_IsArray(root)) {
    cJSON_Delete(root);
    return false;
  }
  std::vector<ScanResult> rows;
  const cJSON* o = nullptr;
  cJSON_ArrayForEach(o, root) {
    if (!cJSON_IsObject(o)) {
      continue;
    }
    ScanResult r;
    get_str(o, "ssid", &r.ssid);
    double rssi = 0;
    if (get_num(o, "rssi", &rssi)) {
      r.rssi = static_cast<int>(rssi);
    }
    get_bool(o, "open", &r.open);
    rows.push_back(std::move(r));
  }
  cJSON_Delete(root);
  *out = std::move(rows);
  return true;
}

bool parse_audio_channels(const char* json, size_t len, AudioChannels* out) {
  if (json == nullptr || out == nullptr) {
    return false;
  }
  cJSON* root = cJSON_ParseWithLength(json, len);
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return false;
  }
  AudioChannels ac;
  get_bool(root, "available", &ac.available);
  const cJSON* arr = obj(root, "channels");
  if (cJSON_IsArray(arr)) {
    const cJSON* o = nullptr;
    cJSON_ArrayForEach(o, arr) {
      if (!cJSON_IsObject(o)) {
        continue;
      }
      AudioChannel c;
      get_str(o, "id", &c.id);
      get_str(o, "name", &c.name);
      get_str(o, "peer", &c.peer);
      get_u32(o, "rate", &c.rate);
      get_u32(o, "channels", &c.channels);
      get_bool(o, "local", &c.local);
      ac.channels.push_back(std::move(c));
    }
  }
  cJSON_Delete(root);
  *out = std::move(ac);
  return true;
}

bool parse_config_secrets(const char* json, size_t len, ConfigSecrets* out) {
  if (json == nullptr || out == nullptr) {
    return false;
  }
  cJSON* root = cJSON_ParseWithLength(json, len);
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return false;
  }
  ConfigSecrets s;
  const cJSON* wifi = obj(root, "wifi");
  const cJSON* nets = cJSON_IsObject(wifi) ? obj(wifi, "networks") : nullptr;
  if (cJSON_IsArray(nets)) {
    int i = 0;
    const cJSON* o = nullptr;
    cJSON_ArrayForEach(o, nets) {
      if (i >= 4) {
        break;
      }
      if (cJSON_IsObject(o)) {
        get_bool(o, "has_pass", &s.wifi_has_pass[i]);
      }
      ++i;
    }
  }
  const cJSON* ap = obj(root, "ap");
  if (cJSON_IsObject(ap)) {
    get_bool(ap, "has_pass", &s.ap_has_pass);
  }
  cJSON_Delete(root);
  *out = s;
  return true;
}

}  // namespace neon::client
