#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neon::client {

inline constexpr const char* kMicKind = "phone-mic";
inline constexpr const char* kMicDefaultHost = "neon-mic.local";
inline constexpr int kMicDefaultPort = 17001;
inline constexpr int kMicPeerNameMax = 23;

// Shown when the MIC tab is bound to a module, or the module tabs to a phone.
inline constexpr const char* kKindMismatchModule =
    "That's a Neon Link module — use the other tabs.";
inline constexpr const char* kKindMismatchMic =
    "That's a Neon Mic phone — use the MIC tab.";

enum class DocumentKind { NeonLink, PhoneMic, NeonInterface, Unknown };

enum class MicPresence { Online, Offline };
enum class MicPermission { Unknown, Denied, Granted };
enum class MetronomeSound { Sine, Noise, Wood };
enum class CaptureOp { Start, Stop, Toggle };
enum class PresenceWord { Live, Local, Idle };

struct MicConfig {
  std::string kind = kMicKind;
  int config_version = 1;
  uint32_t rev = 0;
  std::string peer_name = "iPhone";
  double gain_db = 0;
  std::string source_id;
  bool keep_alive = true;
  bool start_stop_sync = true;
  bool publish = true;
  int jitter_ms = 60;
  bool metronome_enabled = false;
  double metronome_gain_db = -6;
  MetronomeSound metronome_sound = MetronomeSound::Sine;
  bool metronome_accent = true;
  double monitor_gain_db = -60;
};

struct MicStatus {
  std::string kind = kMicKind;
  std::string device_name = "neon-mic";
  std::string hostname = kMicDefaultHost;
  std::string ip;
  std::string firmware = "0.1.0";
  uint32_t rev = 0;
  MicPresence presence = MicPresence::Offline;
  bool streaming = false;
  bool on_session = false;
  MicPermission permission = MicPermission::Unknown;
  bool playing = false;
  double bpm = 0;
  uint32_t phase_milli = 0;
  uint32_t quantum = 4;
  uint32_t peers = 0;
  uint32_t subscribers = 0;
  uint32_t sample_rate = 48000;
  double latency_ms = 0;
  double rms = 0;
  double peak = 0;
  bool clip = false;
  std::string source_label;
  std::string error;  // empty means null on the wire
  uint32_t uptime_s = 0;
};

struct MicSourceRow {
  std::string id;
  std::string label;
};

struct MicHost {
  std::string host = kMicDefaultHost;
  int port = kMicDefaultPort;
};

struct CaptureReply {
  bool ok = false;
  bool streaming = false;
};

MicConfig default_mic_config();
MicStatus default_mic_status();

// Force kind + clamp. Does not require kind on the input (merge path).
void sanitize_mic_config(MicConfig* cfg);
void sanitize_mic_status(MicStatus* st);

// Missing / empty / "neon-link" → NeonLink. Garbage or non-object → Unknown.
DocumentKind probe_document_kind(const char* json, size_t len);

// False when the body is not a phone-mic document. Missing fields take
// defaults; unknown fields are ignored.
bool parse_mic_status(const char* json, size_t len, MicStatus* out);
bool parse_mic_config(const char* json, size_t len, MicConfig* out);
bool parse_mic_sources(const char* json, size_t len, std::vector<MicSourceRow>* out);
bool parse_capture_reply(const char* json, size_t len, CaptureReply* out);

// Partial PUT: kind plus keys that differ. Never includes rev.
std::string mic_config_patch_json(const MicConfig& from, const MicConfig& to);

MicConfig merge_mic_config(const MicConfig& base, const char* json, size_t len);

MicHost parse_mic_host(std::string_view input);

PresenceWord presence_word(const MicStatus& st);

const char* metronome_sound_name(MetronomeSound);
MetronomeSound metronome_sound_from(const char* name);

}  // namespace neon::client
