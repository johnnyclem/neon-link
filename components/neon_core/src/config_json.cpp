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
    case ResetMode::kAtStop:
      return "stop";
    default:
      return "start";
  }
}

const char* role_str(OutputRole r) {
  switch (r) {
    case OutputRole::kGate:
      return "gate";
    case OutputRole::kResetLoop:
      return "reset_loop";
    case OutputRole::kResetStart:
      return "reset_start";
    case OutputRole::kResetStop:
      return "reset_stop";
    default:
      return "clock";
  }
}

const char* rhythm_str(ClockOutputConfig::RhythmMode m) {
  switch (m) {
    case ClockOutputConfig::RhythmMode::kEuclid:
      return "euclid";
    case ClockOutputConfig::RhythmMode::kProbability:
      return "probability";
    case ClockOutputConfig::RhythmMode::kPattern:
      return "pattern";
    default:
      return "all";
  }
}

const char* ap_policy_str(ApPolicy p) {
  switch (p) {
    case ApPolicy::kAlways:
      return "always";
    case ApPolicy::kOff:
      return "off";
    default:
      return "fallback";
  }
}

const char* source_str(ClockSource s) {
  switch (s) {
    case ClockSource::kLinkMaster:
      return "link";
    case ClockSource::kExternalMaster:
      return "external";
    case ClockSource::kMidiMaster:
      return "midi";
    default:
      return "auto";
  }
}

const char* audio_role_str(AudioRole r) {
  switch (r) {
    case AudioRole::kMetronome:
      return "metronome";
    case AudioRole::kClock:
      return "clock";
    case AudioRole::kReset:
      return "reset";
    case AudioRole::kRun:
      return "run";
    case AudioRole::kAmy:
      return "synth";
    case AudioRole::kLinkIn:
      return "link_in";
    case AudioRole::kLineIn:
      return "line_in";
    default:
      return "mix";
  }
}

const char* click_sound_str(ClickSound s) {
  switch (s) {
    case ClickSound::kNoise:
      return "noise";
    case ClickSound::kWood:
      return "wood";
    default:
      return "sine";
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

const char* beat_style_str(BeatStyle s) {
  switch (s) {
    case BeatStyle::kPie:
      return "pie";
    case BeatStyle::kPendulum:
      return "pendulum";
    case BeatStyle::kPulse:
      return "pulse";
    default:
      return "number";
  }
}

const char* mono_theme_str(MonoTheme t) {
  switch (t) {
    case MonoTheme::kInk:
      return "ink";
    case MonoTheme::kDots:
      return "dots";
    case MonoTheme::kHero:
      return "hero";
    case MonoTheme::kConsole:
      return "console";
    case MonoTheme::kGrid:
      return "grid";
    case MonoTheme::kPulse:
      return "pulse";
    case MonoTheme::kNight:
      return "night";
    default:
      return "classic";
  }
}

const char* color_theme_str(ColorTheme t) {
  switch (t) {
    case ColorTheme::kVoid:
      return "void";
    case ColorTheme::kPhosphor:
      return "phosphor";
    case ColorTheme::kAmber:
      return "amber";
    case ColorTheme::kMagenta:
      return "magenta";
    case ColorTheme::kPaper:
      return "paper";
    default:
      return "teal";
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

// Write-only field: only a non-empty string replaces the stored secret,
// so the editor can round-trip the "" placeholder without wiping it.
void get_secret(const cJSON* obj, const char* key, char* out, size_t cap) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsString(v) && v->valuestring != nullptr &&
      v->valuestring[0] != '\0') {
    std::strncpy(out, v->valuestring, cap - 1);
    out[cap - 1] = '\0';
  }
}

bool str_eq(const cJSON* v, const char* s) {
  return cJSON_IsString(v) && v->valuestring != nullptr &&
         std::strcmp(v->valuestring, s) == 0;
}

// Roles travel as names, so an added role never silently reinterprets a
// stored number.
void get_audio_role(const cJSON* obj, const char* key, AudioRole* out) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (str_eq(v, "mix")) {
    *out = AudioRole::kMix;
  } else if (str_eq(v, "metronome")) {
    *out = AudioRole::kMetronome;
  } else if (str_eq(v, "clock")) {
    *out = AudioRole::kClock;
  } else if (str_eq(v, "reset")) {
    *out = AudioRole::kReset;
  } else if (str_eq(v, "run")) {
    *out = AudioRole::kRun;
  } else if (str_eq(v, "synth")) {
    *out = AudioRole::kAmy;
  } else if (str_eq(v, "link_in")) {
    *out = AudioRole::kLinkIn;
  } else if (str_eq(v, "line_in")) {
    *out = AudioRole::kLineIn;
  }
}

// The 64-step pattern mask travels as hex: JSON numbers are doubles and
// would lose the top bits.
void mask_to_hex(uint64_t mask, char* out) {
  static const char kHex[] = "0123456789abcdef";
  for (int i = 0; i < 16; ++i) {
    out[i] = kHex[(mask >> ((15 - i) * 4)) & 0xf];
  }
  out[16] = '\0';
}

void get_mask(const cJSON* obj, const char* key, uint64_t* out) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!cJSON_IsString(v) || v->valuestring == nullptr) {
    return;
  }
  uint64_t mask = 0;
  for (const char* p = v->valuestring; *p != '\0'; ++p) {
    int digit;
    if (*p >= '0' && *p <= '9') {
      digit = *p - '0';
    } else if (*p >= 'a' && *p <= 'f') {
      digit = *p - 'a' + 10;
    } else if (*p >= 'A' && *p <= 'F') {
      digit = *p - 'A' + 10;
    } else {
      return;  // malformed: keep the current mask
    }
    mask = (mask << 4) | static_cast<uint64_t>(digit);
  }
  *out = mask;
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
    cJSON_AddStringToObject(o, "role", role_str(c.role));
    cJSON_AddBoolToObject(o, "free_run", c.free_run);
    cJSON_AddStringToObject(o, "rhythm", rhythm_str(c.rhythm));
    cJSON_AddNumberToObject(o, "euclid_steps", c.euclid_steps);
    cJSON_AddNumberToObject(o, "euclid_fills", c.euclid_fills);
    cJSON_AddNumberToObject(o, "euclid_rot", c.euclid_rot);
    cJSON_AddNumberToObject(o, "probability_pct", c.probability_pct);
    char mask_hex[17];
    mask_to_hex(c.step_mask, mask_hex);
    cJSON_AddStringToObject(o, "step_mask", mask_hex);
    cJSON_AddBoolToObject(o, "rhythm_over_loop", c.rhythm_over_loop);
    cJSON_AddNumberToObject(o, "humanize_pct", c.humanize_pct);
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
  cJSON_AddBoolToObject(engine, "reset_before_edge",
                        cfg.engine.reset_before_edge);
  cJSON_AddNumberToObject(engine, "reset_lead_us", cfg.engine.reset_lead_us);

  cJSON* cv = cJSON_AddObjectToObject(root, "tempo_cv");
  cJSON_AddNumberToObject(cv, "min_bpm", cfg.tempo_cv_min_bpm);
  cJSON_AddNumberToObject(cv, "max_bpm", cfg.tempo_cv_max_bpm);

  cJSON_AddNumberToObject(root, "quantum", cfg.quantum_beats);
  cJSON_AddStringToObject(root, "clock_source", source_str(cfg.clock_source));
  cJSON_AddNumberToObject(root, "clock_in_ppqn", cfg.clock_in_ppqn);
  cJSON_AddNumberToObject(root, "tempo_milli_bpm", cfg.tempo_milli_bpm);
  cJSON_AddBoolToObject(root, "start_stop_sync", cfg.start_stop_sync != 0);
  cJSON_AddNumberToObject(root, "midi_nudge_us", cfg.midi_nudge_us);
  cJSON_AddStringToObject(root, "device_name", cfg.device_name);
  cJSON_AddNumberToObject(root, "display_brightness", cfg.display_brightness);
  cJSON_AddNumberToObject(root, "display_dim_s", cfg.display_dim_s);
  cJSON_AddNumberToObject(root, "display_dim_level", cfg.display_dim_level);
  cJSON_AddBoolToObject(root, "display_portrait", cfg.display_portrait != 0);
  cJSON_AddBoolToObject(root, "big_beat_display", cfg.big_beat_display != 0);
  cJSON_AddStringToObject(root, "beat_style", beat_style_str(cfg.beat_style));
  cJSON_AddStringToObject(root, "color_theme", color_theme_str(cfg.color_theme));
  cJSON_AddStringToObject(root, "mono_theme", mono_theme_str(cfg.mono_theme));

  cJSON* osc = cJSON_AddObjectToObject(root, "osc");
  cJSON_AddBoolToObject(osc, "enabled", cfg.osc_enabled != 0);
  cJSON_AddNumberToObject(osc, "listen_port", cfg.osc_port);
  cJSON_AddStringToObject(osc, "target", cfg.osc_target);

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
  cJSON_AddBoolToObject(ble, "pc_presets", cfg.midi.pc_presets);

  // Stored station list. Passwords are write-only: the editor sees only
  // whether one is set, and echoing "" back leaves it untouched.
  cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
  cJSON* nets = cJSON_AddArrayToObject(wifi, "networks");
  for (const auto& n : cfg.wifi) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ssid", n.ssid);
    cJSON_AddStringToObject(o, "pass", "");
    cJSON_AddBoolToObject(o, "has_pass", n.pass[0] != '\0');
    cJSON_AddBoolToObject(o, "hidden", n.hidden != 0);
    cJSON_AddItemToArray(nets, o);
  }
  cJSON_AddNumberToObject(wifi, "retries", cfg.wifi_retries);
  // Flat aliases for slot 0 keep older clients (and the OLED menu) working.
  cJSON_AddStringToObject(wifi, "ssid", cfg.wifi[0].ssid);
  cJSON_AddStringToObject(wifi, "pass", "");

  const AudioConfig& ac = cfg.audio;
  cJSON* audio = cJSON_AddObjectToObject(root, "audio");
  cJSON_AddBoolToObject(audio, "enabled", ac.enabled != 0);
  cJSON_AddStringToObject(audio, "role_l", audio_role_str(ac.role_l));
  cJSON_AddStringToObject(audio, "role_r", audio_role_str(ac.role_r));
  cJSON_AddBoolToObject(audio, "metro_enabled", ac.metro_enabled != 0);
  cJSON_AddStringToObject(audio, "metro_sound", click_sound_str(ac.metro_sound));
  cJSON_AddNumberToObject(audio, "metro_gain", ac.metro_gain);
  cJSON_AddBoolToObject(audio, "metro_accent", ac.metro_accent != 0);
  cJSON_AddBoolToObject(audio, "amy_enabled", ac.amy_enabled != 0);
  cJSON_AddNumberToObject(audio, "amy_gain", ac.amy_gain);
  cJSON_AddNumberToObject(audio, "amy_patch", ac.amy_patch);
  cJSON_AddNumberToObject(audio, "linein_monitor_gain", ac.linein_monitor_gain);
  cJSON_AddBoolToObject(audio, "publish_mix", ac.la_publish_mix != 0);
  cJSON_AddBoolToObject(audio, "publish_linein", ac.la_publish_linein != 0);
  cJSON_AddBoolToObject(audio, "publish_mono", ac.la_publish_mono != 0);
  cJSON_AddBoolToObject(audio, "gist_lpf", ac.la_fullband == 0);
  cJSON_AddNumberToObject(audio, "sub_gain", ac.la_sub_gain);
  cJSON_AddNumberToObject(audio, "jitter_ms", ac.la_jitter_ms);
  cJSON_AddStringToObject(audio, "channel_name", ac.la_channel_name);
  cJSON_AddStringToObject(audio, "sub_channel_id", ac.la_sub_channel_id);

  cJSON* ap = cJSON_AddObjectToObject(root, "ap");
  cJSON_AddStringToObject(ap, "policy", ap_policy_str(cfg.ap_policy));
  cJSON_AddStringToObject(ap, "ssid", cfg.ap_ssid);
  cJSON_AddStringToObject(ap, "pass", "");
  cJSON_AddBoolToObject(ap, "has_pass", cfg.ap_pass[0] != '\0');
  cJSON_AddBoolToObject(ap, "require_pass", cfg.ap_require_pass != 0);
  cJSON_AddBoolToObject(ap, "hidden", cfg.ap_hidden != 0);
  cJSON_AddNumberToObject(ap, "channel", cfg.ap_channel);

  // Server-generated, first-boot-only secret (G1 in the ship-gate review).
  // Round-tripped in full, unlike the write-only wifi/ap passwords above:
  // the web editor has to read it back out to attach it as the X-Neon-Token
  // header on POST /api/ota and POST /api/factory_reset. There is no setter
  // in config_from_json, so a PUT can never overwrite it — only the
  // firmware itself sets this, once, at first boot.
  cJSON_AddStringToObject(root, "device_token", cfg.device_token);

  // Debug-only test knobs (docs/STUDIO_MODE_TEST_PLAN.md); both default to
  // normal operation. Not surfaced in the editor UI.
  cJSON* debug = cJSON_AddObjectToObject(root, "debug");
  cJSON_AddStringToObject(debug, "priority_profile",
                          priority_profile_str(cfg.priority_profile));
  cJSON_AddBoolToObject(debug, "telemetry_uart_csv",
                        cfg.telemetry_uart_csv != 0);

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
        const cJSON* role = cJSON_GetObjectItemCaseSensitive(o, "role");
        if (str_eq(role, "clock")) {
          c.role = OutputRole::kClock;
        } else if (str_eq(role, "gate")) {
          c.role = OutputRole::kGate;
        } else if (str_eq(role, "reset_loop")) {
          c.role = OutputRole::kResetLoop;
        } else if (str_eq(role, "reset_start")) {
          c.role = OutputRole::kResetStart;
        } else if (str_eq(role, "reset_stop")) {
          c.role = OutputRole::kResetStop;
        }
        get_bool(o, "free_run", &c.free_run);
        const cJSON* rhythm = cJSON_GetObjectItemCaseSensitive(o, "rhythm");
        if (str_eq(rhythm, "all")) {
          c.rhythm = ClockOutputConfig::RhythmMode::kAll;
        } else if (str_eq(rhythm, "euclid")) {
          c.rhythm = ClockOutputConfig::RhythmMode::kEuclid;
        } else if (str_eq(rhythm, "probability")) {
          c.rhythm = ClockOutputConfig::RhythmMode::kProbability;
        } else if (str_eq(rhythm, "pattern")) {
          c.rhythm = ClockOutputConfig::RhythmMode::kPattern;
        }
        get_u8(o, "euclid_steps", &c.euclid_steps);
        get_u8(o, "euclid_fills", &c.euclid_fills);
        get_u8(o, "euclid_rot", &c.euclid_rot);
        get_u8(o, "probability_pct", &c.probability_pct);
        get_mask(o, "step_mask", &c.step_mask);
        get_bool(o, "rhythm_over_loop", &c.rhythm_over_loop);
        get_u8(o, "humanize_pct", &c.humanize_pct);
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
    } else if (str_eq(rm, "stop")) {
      cfg->engine.reset_mode = ResetMode::kAtStop;
    }
    get_u32(engine, "reset_trig_len_us", &cfg->engine.reset_trig_len_us);
    get_bool(engine, "run_enabled", &cfg->engine.run_enabled);
    get_bool(engine, "transport_gating", &cfg->engine.transport_gating);
    get_i32(engine, "latency_us", &cfg->engine.latency_us);
    get_bool(engine, "reset_before_edge", &cfg->engine.reset_before_edge);
    get_u32(engine, "reset_lead_us", &cfg->engine.reset_lead_us);
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
  } else if (str_eq(src, "midi")) {
    cfg->clock_source = ClockSource::kMidiMaster;
  }
  get_u32(root, "clock_in_ppqn", &cfg->clock_in_ppqn);
  get_u32(root, "tempo_milli_bpm", &cfg->tempo_milli_bpm);
  get_bool_u8(root, "start_stop_sync", &cfg->start_stop_sync);
  get_i32(root, "midi_nudge_us", &cfg->midi_nudge_us);
  get_str(root, "device_name", cfg->device_name, sizeof(cfg->device_name));
  get_u8(root, "display_brightness", &cfg->display_brightness);
  get_u16(root, "display_dim_s", &cfg->display_dim_s);
  get_u8(root, "display_dim_level", &cfg->display_dim_level);
  get_bool_u8(root, "display_portrait", &cfg->display_portrait);
  get_bool_u8(root, "big_beat_display", &cfg->big_beat_display);
  {
    const cJSON* style = cJSON_GetObjectItemCaseSensitive(root, "beat_style");
    if (str_eq(style, "pie")) {
      cfg->beat_style = BeatStyle::kPie;
    } else if (str_eq(style, "pendulum")) {
      cfg->beat_style = BeatStyle::kPendulum;
    } else if (str_eq(style, "pulse")) {
      cfg->beat_style = BeatStyle::kPulse;
    } else if (str_eq(style, "number")) {
      cfg->beat_style = BeatStyle::kNumber;
    }
  }
  {
    const cJSON* theme = cJSON_GetObjectItemCaseSensitive(root, "color_theme");
    if (str_eq(theme, "void")) {
      cfg->color_theme = ColorTheme::kVoid;
    } else if (str_eq(theme, "teal")) {
      cfg->color_theme = ColorTheme::kTeal;
    } else if (str_eq(theme, "phosphor")) {
      cfg->color_theme = ColorTheme::kPhosphor;
    } else if (str_eq(theme, "amber")) {
      cfg->color_theme = ColorTheme::kAmber;
    } else if (str_eq(theme, "magenta")) {
      cfg->color_theme = ColorTheme::kMagenta;
    } else if (str_eq(theme, "paper")) {
      cfg->color_theme = ColorTheme::kPaper;
    }
  }
  {
    const cJSON* theme = cJSON_GetObjectItemCaseSensitive(root, "mono_theme");
    if (str_eq(theme, "classic")) {
      cfg->mono_theme = MonoTheme::kClassic;
    } else if (str_eq(theme, "ink")) {
      cfg->mono_theme = MonoTheme::kInk;
    } else if (str_eq(theme, "dots")) {
      cfg->mono_theme = MonoTheme::kDots;
    } else if (str_eq(theme, "hero")) {
      cfg->mono_theme = MonoTheme::kHero;
    } else if (str_eq(theme, "console")) {
      cfg->mono_theme = MonoTheme::kConsole;
    } else if (str_eq(theme, "grid")) {
      cfg->mono_theme = MonoTheme::kGrid;
    } else if (str_eq(theme, "pulse")) {
      cfg->mono_theme = MonoTheme::kPulse;
    } else if (str_eq(theme, "night")) {
      cfg->mono_theme = MonoTheme::kNight;
    }
  }

  const cJSON* osc = cJSON_GetObjectItemCaseSensitive(root, "osc");
  if (cJSON_IsObject(osc)) {
    get_bool_u8(osc, "enabled", &cfg->osc_enabled);
    get_u16(osc, "listen_port", &cfg->osc_port);
    get_str(osc, "target", cfg->osc_target, sizeof(cfg->osc_target));
  }

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
    get_bool(ble, "pc_presets", &cfg->midi.pc_presets);
  }

  const cJSON* wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
  if (cJSON_IsObject(wifi)) {
    const cJSON* nets = cJSON_GetObjectItemCaseSensitive(wifi, "networks");
    if (cJSON_IsArray(nets)) {
      int i = 0;
      const cJSON* o = nullptr;
      cJSON_ArrayForEach(o, nets) {
        if (i >= kWifiSlots || !cJSON_IsObject(o)) {
          break;
        }
        WifiNetwork& n = cfg->wifi[i];
        // A slot whose SSID changed must not inherit the old password.
        char prev_ssid[sizeof(n.ssid)];
        std::memcpy(prev_ssid, n.ssid, sizeof(prev_ssid));
        get_str(o, "ssid", n.ssid, sizeof(n.ssid));
        if (std::strcmp(prev_ssid, n.ssid) != 0) {
          n.pass[0] = '\0';
        }
        get_secret(o, "pass", n.pass, sizeof(n.pass));
        get_bool_u8(o, "hidden", &n.hidden);
        ++i;
      }
    } else {
      // Flat single-network form (older clients): slot 0.
      char prev_ssid[sizeof(cfg->wifi[0].ssid)];
      std::memcpy(prev_ssid, cfg->wifi[0].ssid, sizeof(prev_ssid));
      get_str(wifi, "ssid", cfg->wifi[0].ssid, sizeof(cfg->wifi[0].ssid));
      if (std::strcmp(prev_ssid, cfg->wifi[0].ssid) != 0) {
        cfg->wifi[0].pass[0] = '\0';
      }
      get_secret(wifi, "pass", cfg->wifi[0].pass, sizeof(cfg->wifi[0].pass));
    }
    get_u8(wifi, "retries", &cfg->wifi_retries);
  }

  const cJSON* audio = cJSON_GetObjectItemCaseSensitive(root, "audio");
  if (cJSON_IsObject(audio)) {
    AudioConfig& ac = cfg->audio;
    get_bool_u8(audio, "enabled", &ac.enabled);
    get_audio_role(audio, "role_l", &ac.role_l);
    get_audio_role(audio, "role_r", &ac.role_r);
    get_bool_u8(audio, "metro_enabled", &ac.metro_enabled);
    const cJSON* snd = cJSON_GetObjectItemCaseSensitive(audio, "metro_sound");
    if (str_eq(snd, "sine")) {
      ac.metro_sound = ClickSound::kSine;
    } else if (str_eq(snd, "noise")) {
      ac.metro_sound = ClickSound::kNoise;
    } else if (str_eq(snd, "wood")) {
      ac.metro_sound = ClickSound::kWood;
    }
    get_u8(audio, "metro_gain", &ac.metro_gain);
    get_bool_u8(audio, "metro_accent", &ac.metro_accent);
    get_bool_u8(audio, "amy_enabled", &ac.amy_enabled);
    get_u8(audio, "amy_gain", &ac.amy_gain);
    get_u8(audio, "amy_patch", &ac.amy_patch);
    get_u8(audio, "linein_monitor_gain", &ac.linein_monitor_gain);
    get_bool_u8(audio, "publish_mix", &ac.la_publish_mix);
    get_bool_u8(audio, "publish_linein", &ac.la_publish_linein);
    get_bool_u8(audio, "publish_mono", &ac.la_publish_mono);
    {
      const cJSON* gist = cJSON_GetObjectItemCaseSensitive(audio, "gist_lpf");
      if (cJSON_IsBool(gist)) {
        ac.la_fullband = cJSON_IsTrue(gist) ? 0 : 1;
      }
    }
    get_u8(audio, "sub_gain", &ac.la_sub_gain);
    get_u16(audio, "jitter_ms", &ac.la_jitter_ms);
    get_str(audio, "channel_name", ac.la_channel_name,
            sizeof(ac.la_channel_name));
    get_str(audio, "sub_channel_id", ac.la_sub_channel_id,
            sizeof(ac.la_sub_channel_id));
  }

  const cJSON* ap = cJSON_GetObjectItemCaseSensitive(root, "ap");
  if (cJSON_IsObject(ap)) {
    const cJSON* pol = cJSON_GetObjectItemCaseSensitive(ap, "policy");
    if (str_eq(pol, "fallback")) {
      cfg->ap_policy = ApPolicy::kFallback;
    } else if (str_eq(pol, "always")) {
      cfg->ap_policy = ApPolicy::kAlways;
    } else if (str_eq(pol, "off")) {
      cfg->ap_policy = ApPolicy::kOff;
    }
    get_str(ap, "ssid", cfg->ap_ssid, sizeof(cfg->ap_ssid));
    get_secret(ap, "pass", cfg->ap_pass, sizeof(cfg->ap_pass));
    get_bool_u8(ap, "require_pass", &cfg->ap_require_pass);
    get_bool_u8(ap, "hidden", &cfg->ap_hidden);
    get_u8(ap, "channel", &cfg->ap_channel);
  }

  const cJSON* debug = cJSON_GetObjectItemCaseSensitive(root, "debug");
  if (cJSON_IsObject(debug)) {
    const cJSON* pp =
        cJSON_GetObjectItemCaseSensitive(debug, "priority_profile");
    if (str_eq(pp, "fixed")) {
      cfg->priority_profile = PriorityProfile::kFixed;
    } else if (str_eq(pp, "legacy")) {
      cfg->priority_profile = PriorityProfile::kLegacy;
    }
    get_bool_u8(debug, "telemetry_uart_csv", &cfg->telemetry_uart_csv);
  }

  cJSON_Delete(root);
  config_sanitize(cfg);
  return true;
}

}  // namespace neon
