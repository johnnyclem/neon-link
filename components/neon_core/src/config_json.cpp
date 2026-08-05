#include "neon/config/json.hpp"

#include <cstring>

#include "cJSON.h"

namespace neon {

namespace {

const char* reset_mode_str(ResetMode m) {
  switch (m) {
    case ResetMode::kEveryBar:
      return "bar";
    case ResetMode::kOff:
      return "off";
    default:
      return "start";
  }
}

const char* source_str(ClockSource s) {
  switch (s) {
    case ClockSource::kLinkMaster:
      return "link";
    case ClockSource::kExternalMaster:
      return "external";
    default:
      return "auto";
  }
}

const char* policy_str(MidiRouteConfig::ClockPolicy p) {
  switch (p) {
    case MidiRouteConfig::ClockPolicy::kReplace:
      return "replace";
    case MidiRouteConfig::ClockPolicy::kMerge:
      return "merge";
    default:
      return "ignore";
  }
}

void get_u32(const cJSON* obj, const char* key, uint32_t* out) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsNumber(v)) {
    *out = static_cast<uint32_t>(v->valuedouble);
  }
}

void get_u16(const cJSON* obj, const char* key, uint16_t* out) {
  uint32_t v = *out;
  get_u32(obj, key, &v);
  *out = static_cast<uint16_t>(v);
}

void get_u8(const cJSON* obj, const char* key, uint8_t* out) {
  uint32_t v = *out;
  get_u32(obj, key, &v);
  *out = static_cast<uint8_t>(v);
}

void get_i32(const cJSON* obj, const char* key, int32_t* out) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsNumber(v)) {
    *out = static_cast<int32_t>(v->valuedouble);
  }
}

void get_bool(const cJSON* obj, const char* key, bool* out) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsBool(v)) {
    *out = cJSON_IsTrue(v);
  }
}

void get_bool_u8(const cJSON* obj, const char* key, uint8_t* out) {
  bool b = *out != 0;
  get_bool(obj, key, &b);
  *out = b ? 1 : 0;
}

void get_str(const cJSON* obj, const char* key, char* out, size_t cap) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsString(v) && v->valuestring != nullptr) {
    std::strncpy(out, v->valuestring, cap - 1);
    out[cap - 1] = '\0';
  }
}

bool str_eq(const cJSON* v, const char* s) {
  return cJSON_IsString(v) && v->valuestring != nullptr &&
         std::strcmp(v->valuestring, s) == 0;
}

}  // namespace

size_t config_to_json(const Config& cfg, char* buf, size_t cap) {
  cJSON* root = cJSON_CreateObject();

  cJSON* engine = cJSON_AddObjectToObject(root, "engine");
  cJSON* clocks = cJSON_AddArrayToObject(engine, "clocks");
  for (const auto& c : cfg.engine.clocks) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", c.enabled);
    cJSON_AddNumberToObject(o, "ppqn", c.ppqn);
    cJSON_AddNumberToObject(o, "mult", c.mult);
    cJSON_AddNumberToObject(o, "div", c.div);
    cJSON_AddStringToObject(
        o, "mode",
        c.mode == ClockOutputConfig::PulseMode::kSquare ? "square" : "trig");
    cJSON_AddNumberToObject(o, "trig_len_us", c.trig_len_us);
    cJSON_AddNumberToObject(o, "duty_pct", c.duty_pct);
    cJSON_AddNumberToObject(o, "shuffle_pct", c.shuffle_pct);
    cJSON_AddItemToArray(clocks, o);
  }
  cJSON_AddStringToObject(engine, "reset_mode",
                          reset_mode_str(cfg.engine.reset_mode));
  cJSON_AddNumberToObject(engine, "reset_trig_len_us",
                          cfg.engine.reset_trig_len_us);
  cJSON_AddBoolToObject(engine, "run_enabled", cfg.engine.run_enabled);
  cJSON_AddBoolToObject(engine, "transport_gating",
                        cfg.engine.transport_gating);
  cJSON_AddNumberToObject(engine, "latency_us", cfg.engine.latency_us);

  cJSON* cv = cJSON_AddObjectToObject(root, "tempo_cv");
  cJSON_AddNumberToObject(cv, "min_bpm", cfg.tempo_cv_min_bpm);
  cJSON_AddNumberToObject(cv, "max_bpm", cfg.tempo_cv_max_bpm);

  cJSON_AddNumberToObject(root, "quantum", cfg.quantum_beats);
  cJSON_AddStringToObject(root, "clock_source", source_str(cfg.clock_source));
  cJSON_AddNumberToObject(root, "clock_in_ppqn", cfg.clock_in_ppqn);

  cJSON* ble = cJSON_AddObjectToObject(root, "ble");
  cJSON_AddBoolToObject(ble, "enabled", cfg.ble_enabled != 0);
  cJSON_AddBoolToObject(ble, "midi_clock_out", cfg.midi_clock_out != 0);
  cJSON_AddNumberToObject(ble, "channel", cfg.midi.midi_channel);
  cJSON_AddNumberToObject(ble, "gate_target", cfg.midi.gate_target);
  cJSON_AddBoolToObject(ble, "pitch_cv", cfg.midi.pitch_cv);
  cJSON_AddNumberToObject(ble, "cc_latency", cfg.midi.cc_latency);
  cJSON_AddNumberToObject(ble, "cc_shuffle_base", cfg.midi.cc_shuffle_base);
  cJSON_AddStringToObject(ble, "clock_policy",
                          policy_str(cfg.midi.clock_policy));
  cJSON_AddBoolToObject(ble, "transport_enabled",
                        cfg.midi.transport_enabled);

  cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
  cJSON_AddStringToObject(wifi, "ssid", cfg.wifi_ssid);
  cJSON_AddStringToObject(wifi, "pass", "");  // write-only

  const bool ok = cJSON_PrintPreallocated(root, buf, static_cast<int>(cap),
                                          /*fmt=*/false);
  cJSON_Delete(root);
  if (!ok) {
    return 0;
  }
  return std::strlen(buf);
}

bool config_from_json(const char* json, size_t len, Config* cfg) {
  cJSON* root = cJSON_ParseWithLength(json, len);
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return false;
  }

  const cJSON* engine = cJSON_GetObjectItemCaseSensitive(root, "engine");
  if (cJSON_IsObject(engine)) {
    const cJSON* clocks = cJSON_GetObjectItemCaseSensitive(engine, "clocks");
    if (cJSON_IsArray(clocks)) {
      int i = 0;
      const cJSON* o = nullptr;
      cJSON_ArrayForEach(o, clocks) {
        if (i >= 4 || !cJSON_IsObject(o)) {
          break;
        }
        ClockOutputConfig& c = cfg->engine.clocks[i];
        get_bool(o, "enabled", &c.enabled);
        get_u32(o, "ppqn", &c.ppqn);
        get_u32(o, "mult", &c.mult);
        get_u32(o, "div", &c.div);
        const cJSON* mode = cJSON_GetObjectItemCaseSensitive(o, "mode");
        if (str_eq(mode, "square")) {
          c.mode = ClockOutputConfig::PulseMode::kSquare;
        } else if (str_eq(mode, "trig")) {
          c.mode = ClockOutputConfig::PulseMode::kTrigger;
        }
        get_u32(o, "trig_len_us", &c.trig_len_us);
        get_u8(o, "duty_pct", &c.duty_pct);
        get_u8(o, "shuffle_pct", &c.shuffle_pct);
        ++i;
      }
    }
    const cJSON* rm = cJSON_GetObjectItemCaseSensitive(engine, "reset_mode");
    if (str_eq(rm, "start")) {
      cfg->engine.reset_mode = ResetMode::kStartOfPlay;
    } else if (str_eq(rm, "bar")) {
      cfg->engine.reset_mode = ResetMode::kEveryBar;
    } else if (str_eq(rm, "off")) {
      cfg->engine.reset_mode = ResetMode::kOff;
    }
    get_u32(engine, "reset_trig_len_us", &cfg->engine.reset_trig_len_us);
    get_bool(engine, "run_enabled", &cfg->engine.run_enabled);
    get_bool(engine, "transport_gating", &cfg->engine.transport_gating);
    get_i32(engine, "latency_us", &cfg->engine.latency_us);
  }

  const cJSON* cv = cJSON_GetObjectItemCaseSensitive(root, "tempo_cv");
  if (cJSON_IsObject(cv)) {
    get_u16(cv, "min_bpm", &cfg->tempo_cv_min_bpm);
    get_u16(cv, "max_bpm", &cfg->tempo_cv_max_bpm);
  }

  get_u32(root, "quantum", &cfg->quantum_beats);
  const cJSON* src = cJSON_GetObjectItemCaseSensitive(root, "clock_source");
  if (str_eq(src, "auto")) {
    cfg->clock_source = ClockSource::kAuto;
  } else if (str_eq(src, "link")) {
    cfg->clock_source = ClockSource::kLinkMaster;
  } else if (str_eq(src, "external")) {
    cfg->clock_source = ClockSource::kExternalMaster;
  }
  get_u32(root, "clock_in_ppqn", &cfg->clock_in_ppqn);

  const cJSON* ble = cJSON_GetObjectItemCaseSensitive(root, "ble");
  if (cJSON_IsObject(ble)) {
    get_bool_u8(ble, "enabled", &cfg->ble_enabled);
    get_bool_u8(ble, "midi_clock_out", &cfg->midi_clock_out);
    get_u8(ble, "channel", &cfg->midi.midi_channel);
    get_u8(ble, "gate_target", &cfg->midi.gate_target);
    get_bool(ble, "pitch_cv", &cfg->midi.pitch_cv);
    get_u8(ble, "cc_latency", &cfg->midi.cc_latency);
    get_u8(ble, "cc_shuffle_base", &cfg->midi.cc_shuffle_base);
    const cJSON* pol = cJSON_GetObjectItemCaseSensitive(ble, "clock_policy");
    if (str_eq(pol, "ignore")) {
      cfg->midi.clock_policy = MidiRouteConfig::ClockPolicy::kIgnore;
    } else if (str_eq(pol, "replace")) {
      cfg->midi.clock_policy = MidiRouteConfig::ClockPolicy::kReplace;
    } else if (str_eq(pol, "merge")) {
      cfg->midi.clock_policy = MidiRouteConfig::ClockPolicy::kMerge;
    }
    get_bool(ble, "transport_enabled", &cfg->midi.transport_enabled);
  }

  const cJSON* wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
  if (cJSON_IsObject(wifi)) {
    get_str(wifi, "ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid));
    // Only overwrite the stored password when a non-empty one is sent
    // (the editor round-trips the write-only "" placeholder).
    const cJSON* pass = cJSON_GetObjectItemCaseSensitive(wifi, "pass");
    if (cJSON_IsString(pass) && pass->valuestring != nullptr &&
        pass->valuestring[0] != '\0') {
      std::strncpy(cfg->wifi_pass, pass->valuestring,
                   sizeof(cfg->wifi_pass) - 1);
      cfg->wifi_pass[sizeof(cfg->wifi_pass) - 1] = '\0';
    }
  }

  cJSON_Delete(root);
  config_sanitize(cfg);
  return true;
}

}  // namespace neon
