#pragma once

#include <cstddef>
#include <cstdint>

#include "neon/audio/types.hpp"
#include "neon/midi/router.hpp"
#include "neon/multi_engine.hpp"
#include "neon/output_config.hpp"

namespace neon {

// The single persisted configuration struct (one NVS blob). Fixed-width
// members only; encode/decode add a header with magic, version, size, and
// CRC so a corrupt or foreign blob falls back to defaults. Extend by
// appending fields and bumping kConfigVersion (decode rejects unknown
// versions and the caller uses defaults — explicit migrations can be
// added when there is a fleet to migrate).
enum class ClockSource : uint8_t {
  kAuto = 0,            // external when CLK IN is active, Link otherwise
  kLinkMaster = 1,      // ignore CLK IN
  kExternalMaster = 2,  // follow CLK IN whenever it is active
};

// One stored WiFi station credential. The module walks the list in order
// on boot, attempting each network wifi_retries times before moving on.
struct WifiNetwork {
  char ssid[33] = {};
  char pass[65] = {};
  uint8_t hidden = 0;  // network does not broadcast its SSID
  uint8_t pad_[3] = {};
};

inline constexpr int kWifiSlots = 4;

// Everything the audio engine owns, in one block so the v3 → v4 migration
// is a single assignment rather than a list of fields to remember. Sizes
// are laid out by hand: 16 bytes of flags before the 16-bit jitter figure,
// so there is no implicit padding to reason about.
struct AudioConfig {
  // Master switch. Restart-scoped: turning audio on starts the I2S task.
  uint8_t enabled = 0;
  AudioRole role_l = AudioRole::kMix;
  AudioRole role_r = AudioRole::kMix;
  uint8_t metro_enabled = 0;

  ClickSound metro_sound = ClickSound::kSine;
  uint8_t metro_gain = kUnityGainByte;
  uint8_t metro_accent = 1;
  uint8_t amy_enabled = 0;

  uint8_t amy_gain = kUnityGainByte;
  uint8_t amy_patch = 0;
  uint8_t linein_monitor_gain = 0;  // 0 = line in is not monitored
  uint8_t la_publish_mix = 0;       // publish "<name> Out" (the master mix)

  uint8_t la_publish_linein = 0;    // publish "<name> In" (the line-in tap)
  uint8_t la_publish_mono = 0;      // halve the bitrate on a busy network
  uint8_t la_sub_gain = kUnityGainByte;
  // 0 = 120 Hz–5 kHz gist band on subscribe (the pad this replaced). Kept
  // inverted so existing v4 NVS blobs (this byte was 0) turn the filter on.
  uint8_t la_fullband = 0;

  uint16_t la_jitter_ms = 60;

  // Published channel name; empty derives it from device_name.
  char la_channel_name[24] = "";
  // The channel we subscribe to; empty means not subscribed.
  char la_sub_channel_id[48] = "";
};

// The Config::ap_pass member's struct-literal default, and the fallback a
// too-short/empty password sanitizes to. This is NOT what a shipping unit
// actually boots with: first boot (no valid stored config) overwrites it
// with derive_ap_pass_from_mac()'s per-device password before the AP ever
// comes up (components/app_state/src/config_store.cpp). A shared,
// documented default would put every unit's setup AP behind the same key;
// this constant only exists as the safe fallback config_sanitize() falls
// back to when the stored password fails the 8-char WPA2 minimum.
inline constexpr char kDefaultApPass[] = "link1234";

// When the module creates its own network.
enum class ApPolicy : uint8_t {
  kFallback = 0,  // only when no stored network can be joined
  kAlways = 1,    // never join a stored network; always self-host
  kOff = 2,       // never create an access point
};

// Debug-only override of the FreeRTOS priorities Link's asio service task
// and the Link Audio pump task run at. kFixed is the shipped, corrected
// ordering (asio above the pump); kLegacy reproduces the pre-fix inversion
// (docs/STUDIO_MODE_TEST_PLAN.md Phase 2 needs both, on demand, without a
// separate firmware build, to run the priority-fix regression as an A/B
// rather than take it on faith).
enum class PriorityProfile : uint8_t {
  kFixed = 0,
  kLegacy = 1,
};

// Full-screen beat animation on the live panel (and its web echo) while
// the transport is running. Number is the original giant 1/2/3/4.
enum class BeatStyle : uint8_t {
  kNumber = 0,
  kPie = 1,
  kPendulum = 2,
  kPulse = 3,
  kCount = 4,
};

struct Config {
  EngineConfig engine;

  // Tempo CV mapping: BPM range that spans the 0..5 V output.
  uint16_t tempo_cv_min_bpm = 20;
  uint16_t tempo_cv_max_bpm = 300;

  uint32_t quantum_beats = 4;

  // Bidirectional operation (milestone 5).
  ClockSource clock_source = ClockSource::kAuto;
  uint32_t clock_in_ppqn = 4;  // pulses per beat expected on CLK IN

  // BLE MIDI (milestone 7). ble_enabled=0 keeps the BT controller fully
  // deinitialized for maximum Link reliability (the SOFTWARE.md kill
  // switch).
  uint8_t ble_enabled = 1;
  uint8_t midi_clock_out = 1;  // 24 PPQN Link-derived clock on TRS

  // Ableton Link 3 start/stop sync: follow (and broadcast) transport
  // changes from other peers. Off means the local transport is private.
  uint8_t start_stop_sync = 1;

  // Separate from engine.latency_us: MIDI gear has its own input latency,
  // so the TRS/BLE clock stream can be nudged independently of the CV
  // outputs. Range mirrors the legacy editors' -10..+100 ms, widened.
  int32_t midi_nudge_us = 0;

  // Tempo used when no Link peer dictates one (tap tempo, +/- nudge, and
  // the editor's tempo field all land here so it survives a reboot).
  uint32_t tempo_milli_bpm = 120000;

  MidiRouteConfig midi;

  // Identity: drives the mDNS hostname (<name>.local), the default access
  // point SSID, and the editor's title.
  char device_name[24] = "neon-link";

  // Display / indicator brightness, 0..255 (0 blanks the panel).
  uint8_t display_brightness = 255;

  // WiFi station list. Slot 0 is what the single-network editor and the
  // menuconfig defaults write.
  WifiNetwork wifi[kWifiSlots];
  uint8_t wifi_retries = 3;  // attempts per network before moving on

  // Access point. Secured by default: the fallback AP is the first-boot
  // path, and an open network puts every unauthenticated /api endpoint —
  // including OTA — in reach of anyone in RF range.
  ApPolicy ap_policy = ApPolicy::kFallback;
  uint8_t ap_require_pass = 1;  // 0 = open network (easy Link jams)
  uint8_t ap_hidden = 0;
  uint8_t ap_channel = 1;
  char ap_ssid[33] = {};  // empty = derive from device_name + MAC
  char ap_pass[65] = "link1234";

  // Full-screen beat number (1, 2, 3, 4…) while the transport is running.
  // Appended in v3 so a v2 NVS blob still decodes (new field keeps default).
  // On by default: 1 and 3 are white on black, 2 and 4 are black on white.
  uint8_t big_beat_display = 1;

  // Appended in v4 (docs/AUDIOLINK.md). A v3 blob decodes with the whole
  // block back at its defaults — see config_decode.
  AudioConfig audio;

  // Appended in v5 (docs/STUDIO_MODE_TEST_PLAN.md). Debug-only test knobs,
  // both default to normal operation: priority_profile reproduces the
  // pre-fix task-priority inversion on demand (see the enum above), and
  // telemetry_uart_csv turns on the one-line-per-second CSV telemetry
  // stream the plan's P4 wants for offline analysis.
  PriorityProfile priority_profile = PriorityProfile::kFixed;
  uint8_t telemetry_uart_csv = 0;

  // Appended in v6. A per-device secret, generated once at first boot
  // (never a fleet-wide constant) and never accepted back from the web
  // editor's PUT /api/config — config_from_json has no setter for it, so
  // it can only be set here, in NVS, by the firmware itself. Sent as the
  // X-Neon-Token header, it gates POST /api/ota and POST /api/factory_reset:
  // check_local_origin's Host/Origin check already stops a browser from
  // forging those requests cross-site, but it cannot stop a non-browser
  // client on the same LAN or AP from setting an arbitrary Host header by
  // hand. 32 hex chars (128 bits) plus NUL.
  char device_token[33] = "";

  // Appended in v7. Which beat animation the live screen draws. Independent
  // of big_beat_display, which remains the on/off. A v6 blob decodes with
  // this back at kNumber — see config_decode.
  BeatStyle beat_style = BeatStyle::kNumber;
};

inline constexpr uint32_t kConfigMagic = 0x4e4c4346;  // "NLCF"
inline constexpr uint16_t kConfigVersion = 7;

// Tempo limits shared by the tap estimator, the editor, and the encoder.
inline constexpr uint32_t kMinMilliBpm = 20000;
inline constexpr uint32_t kMaxMilliBpm = 999000;

// Reduce an arbitrary user-entered name to a DNS-safe label (lowercase
// alphanumerics and single hyphens, no leading/trailing hyphen). Falls
// back to "neon-link" when nothing usable survives. Returns the length
// written; `out` is always NUL-terminated.
size_t sanitize_hostname(const char* in, char* out, size_t cap);

// The SSID the module advertises in access point mode: the explicit
// ap_ssid when set, otherwise "<DEVICE-NAME>-XXXX" from the MAC.
size_t ap_ssid_for(const Config& cfg, const uint8_t mac[6], char* out,
                   size_t cap);

// SSID/password/policy/SoftAP identity. Used to bounce STA and to
// exempt those fields from the G6 NVS hold — a network Save that only
// lives in RAM reboots back to the old AP policy.
bool network_identity_changed(const Config& a, const Config& b);

// This unit's setup AP password, derived from its own MAC so a fleet of
// units never shares one key (G1 in the ship-gate review: a printed,
// fleet-wide default is one Google search away from an open AP). Always
// >= 8 characters (WPA2's minimum), independent of `mac`'s contents.
// Deterministic so config_store.cpp can call it again on every boot if it
// ever needs to re-derive rather than only at first boot.
size_t derive_ap_pass_from_mac(const uint8_t mac[6], char* out, size_t cap);

// Wire size of an encoded config blob.
size_t config_blob_size();

// Serialize with header + CRC. `cap` must be >= config_blob_size().
// Returns bytes written, or 0 on insufficient capacity.
size_t config_encode(const Config& cfg, uint8_t* buf, size_t cap);

// Validate header, version, and CRC; on success fills `out` (clamped to
// valid ranges). Returns false on any mismatch — caller keeps defaults.
bool config_decode(const uint8_t* buf, size_t len, Config* out);

// Clamp all fields into their valid ranges (also applied by decode).
void config_sanitize(Config* cfg);

// CRC-32 (IEEE, reflected), for the config blob and later preset slots.
uint32_t crc32(const uint8_t* data, size_t len);

// The live-applied slice the audio task reads through its seqlock. The
// restart-scoped fields (publish flags, subscription) stay behind on
// core 0, which is the only place that can act on them.
AudioEngineConfig audio_engine_config(const Config& cfg);

// The FreeRTOS priority Link's asio service task and the Link Audio pump
// task run at, given `profile`. Pulled out as pure functions so the
// pre-fix/post-fix numbers are host-testable without dragging in FreeRTOS:
// components/ableton_link/link_overrides/.../Context.hpp and
// components/ableton_link/src/link_audio_esp.cpp read these (via
// ablink::set_link_asio_priority / set_link_pump_priority, applied once at
// boot) instead of carrying the numbers as hardcoded constants.
int link_asio_task_priority(PriorityProfile profile);
int link_pump_task_priority(PriorityProfile profile);

// "fixed" | "legacy" — the single source of truth for how a PriorityProfile
// (or AudioStatus::priority_profile, which mirrors it as a plain uint8_t —
// see neon/audio/types.hpp) is spelled over JSON/CSV. Both
// components/neon_core/src/config_json.cpp and
// components/web_ui/src/web_ui.cpp call this rather than each carrying
// their own copy of the mapping.
const char* priority_profile_str(PriorityProfile profile);

// The name the module publishes its mix under: the explicit
// audio.la_channel_name when set, otherwise "<Device Name> Out" / " In".
size_t audio_channel_name(const Config& cfg, bool line_in, char* out,
                          size_t cap);

}  // namespace neon
