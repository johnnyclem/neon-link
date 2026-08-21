# AudioLink — Design Specification

**Version**: 1.0
**Date**: 2026-08-12
**Status**: Implemented — see §12 for what shipped, what is gated on
hardware, and the two submodules that are not in this tree.
A proposal to supersede Link Audio streaming as the multi-player audio
strategy — pre-distributed stems, events on the wire — is
[`STEM_SYNC.md`](STEM_SYNC.md).
**Related**: [`ARCHITECTURE.md`](ARCHITECTURE.md) · [`AMYBOARD.md`](AMYBOARD.md) · [`FEATURES.md`](FEATURES.md) · [`../SOFTWARE.md`](../SOFTWARE.md)

---

## 1. Context & Motivation

NEON LINK is today a *control-rate* Ableton Link peer: tempo/phase/transport in,
clock/CV/MIDI out. The AMYboard's entire audio path — PCM5101 stereo I2S DAC
out, PCM1808 stereo I2S ADC in, switchable 1 Vpp line / 10 Vpp modular levels,
S/PDIF — is completely unused, and both requirement docs (`HARDWARE.md` §1.3,
`SOFTWARE.md` §3) deferred "Link Audio streaming."

That deferral is obsolete: **Ableton shipped Link Audio in Link 4.0** (tag
`Link-4.0`, May 2026; exposed to users in Live 12.4). It is part of the same
open-source SDK this repo already vendors (currently pinned at Link-3.1.5).
AudioLink turns the module into a full audio peer:

1. **Link audio engine** — I2S audio rendered sample-accurately on the Link
   timeline.
2. **Metronome click** — accented downbeat, selectable sounds, volume.
3. **Clock/reset/run pulses as audio** — per-channel roles, ~23 µs (1-sample)
   jitter vs ~1 ms on the I2C CV path.
4. **AMY synth voice** — vendor shorepine/amy; BLE/TRS MIDI notes → Link-synced
   voice in the mix.
5. **Link Audio streaming (headline)** — publish named channels (master mix,
   line-in) to Live 12.4 / peers; subscribe to remote channels and play them
   out — exactly the way Live 12.4 does with Link Audio.
6. **Line-in passthrough/publish** — "send my modular into Live over WiFi."
7. Full config/web/OLED integration, host tests, CI legs.

### Established facts

- Zero audio code exists in the tree (no I2S/codec/click/`AudioSessionState`
  reference anywhere). Greenfield subsystem.
- The Link wrapper uses only `captureAppSessionState` on the 10 ms `link_svc`
  poll; core 1 consumes an integer `TimelineSnapshot` (µs/beat Q32.32) via
  `neon::SeqLock`; **doubles are banned from RT paths** (the S3 FPU is
  single-precision).
- Link Audio API (Link 4.0): `ableton::LinkAudio` replaces `ableton::Link`
  (`#include <ableton/LinkAudio.hpp>` in every using TU);
  `LinkAudioSink(name, maxSamples)` publishes a named channel (transmits only
  while ≥1 subscriber); `LinkAudioSource(id, callback)` receives **interleaved
  int16 at the sender's rate/block size** with `info.beginBeats()/endBeats()`
  for beat mapping; the receiver owns ring-buffering + sample-rate conversion
  (Live's default 48 kHz vs our 44.1 kHz).
- AMYboard I2S GPIOs are **not in this repo** — they come from the shorepine
  AMYboard schematic/board definition at PR1 bring-up and land in
  `board_pins.h` with the `-1` sentinel pattern. The PCM1808 needs a 256fs
  MCLK — confirm routing. Both codecs are hardware-configured (no control
  port expected — confirm).
- `partitions.csv` has 3 MB OTA slots (an 8 MB-flash layout) while the
  AMYboard has 16 MB flash — Link 4 + AMY will likely bust 3 MB, so a 16 MB
  partition table is in scope.

---

## 2. Headline decisions

| Decision | Choice | Why |
|---|---|---|
| Beat source for audio | **`TimelineSnapshot` via `timeline_bus()` seqlock**, not `captureAudioSessionState` | The snapshot is already a linear beat↔µs map; sample-accuracy then depends only on the sample-clock correlation. Preserves the doubles ban, the single-owner session model, and makes the stub leg free. The seam allows swapping later if tempo-ramp granularity ever matters. |
| Audio task placement | **Core 1, prio `configMAX_PRIORITIES - 4`** (below pulse MAX-2, cv_mirror MAX-3) | I2S DMA gives ~11 ms queue slack, so audio tolerates preemption; core 0's WiFi/asio/httpd bursts are the worse neighbor. Gate: scope-verify pulse jitter unchanged. |
| Block/DMA geometry | **44.1 kHz stereo int16, 128 frames/block, 4 DMA descriptors** | 2.9 ms render period, ~11.6 ms worst-case output latency; matches AMY's native 44.1 k. |
| Sample↔µs correlation | Integer **PLL-style drift servo** (`neon::SampleClock`, Q32.32/ppm), fed by DMA-consumption timestamps | Doubles banned on core 1; host-testable; fills the HostTimeFilter role from Ableton's linkaudio examples. |
| DSP number format | float32 internally, int16 at I2S/network boundaries | The S3 FPU is single-precision hardware; only doubles are banned. |
| Link Audio network I/O | **Never on the audio task** — SPSC rings + a core-0 "linkaudio pump" task (prio 11) | lwIP stays off core 1; rings decouple RT from network jitter in both directions. |
| Receiver SRC | Linear-interpolation resampler + fill-level ratio servo | Adequate for monitoring/jamming, cheap, host-testable; a windowed-sinc upgrade slots in behind the same interface later. |
| AMY integration | New `components/amy_synth` over a `third_party/amy` submodule, `CONFIG_NEON_AUDIO_AMY` | Keeps the MIT C dependency out of neon_core; stub-able for CI. |
| S/PDIF | Deferred | Separate peripheral, no interaction with this design. |

License: Link stays GPLv2+/commercial (unchanged by 4.0); AMY is MIT — no new
obligations beyond what shipping Link already implies.

---

## 3. File layout

**Portable (`components/neon_core`**, host-tested, added to both CMake branches):

```
include/neon/audio/sample_clock.hpp   src/audio/sample_clock.cpp   # µs↔frame PLL
include/neon/audio/click.hpp          src/audio/click.cpp          # ClickSynth (sine/noise/wood)
include/neon/audio/pulse_render.hpp   src/audio/pulse_render.cpp   # clock/reset/run → samples
include/neon/audio/mixer.hpp          src/audio/mixer.cpp          # routing matrix + gains + soft clip
include/neon/audio/frame_ring.hpp                                  # header-only SPSC frame ring
include/neon/audio/resampler.hpp      src/audio/resampler.cpp      # linear SRC + ratio servo
include/neon/audio/jitter_buffer.hpp  src/audio/jitter_buffer.cpp  # beat-aligned receive buffer
include/neon/audio/beat_window.hpp                                 # snapshot → per-block beat window (int math)
```

**HAL (`components/neon_hal`)**: `include/hal/IAudioIo.hpp`,
`include/hal/ILinkAudio.hpp` — mirror the `ILinkSession.hpp` seam pattern.

**ESP drivers (`components/neon_hal_esp`)**: `src/i2s_audio.cpp` +
`include/hal_esp/i2s_audio.h` — IDF 5.3 `i2s_std` duplex on I2S0 (TX+RX share
BCLK/WS), `on_sent` ISR callback records `(esp_timer µs, cumulative frames)`
marks through an ISR-safe seqlock; DMA buffers in internal RAM, large rings in
PSRAM (`MALLOC_CAP_SPIRAM`).

**Link (`components/ableton_link`)**: `include/ablink/audio.hpp`
(`ablink::link_audio() -> hal::ILinkAudio&`), `src/link_audio_esp.cpp` (real
impl + pump task), `link_stub.cpp` grows a no-op `ILinkAudio`. One
`ableton::LinkAudio` instance backs both facades; `ablink::session()` is
unchanged for existing callers.

**AMY**: `components/amy_synth` (CMakeLists, Kconfig `NEON_AUDIO_AMY`,
`src/amy_synth.cpp` + `src/amy_stub.cpp`) wrapping submodule `third_party/amy`.

**App state (`components/app_state`)**: `include/app_state/audio_bus.h` —
`AudioEngineConfig` seqlock (config → audio task) and `AudioStatus` seqlock
(underruns, peaks, publish/subscribe state → web/OLED).

**Main**: `main/audio_service.cpp` (modeled on `link_service.cpp`), entry in
`tasks.h`, called from `app_main.cpp`; `main/Kconfig.projbuild` gains
`NEON_AUDIO` (default y for AMYBOARD).

**Board pins (`components/neon_board/include/board_pins.h`)**, AMYBOARD block
(CUSTOM block gets `-1`): `kPinI2sMclk / kPinI2sBclk / kPinI2sLrclk /
kPinI2sDout / kPinI2sDin` — values from the AMYboard schematic (PR1 task 1).
If the PCM1808 runs from its own crystal instead of ESP MCLK, the input side
gets its own `SampleClock` instance.

**Partitions**: new `partitions_16mb.csv` (6 MB ota_0/ota_1), referenced from
`sdkconfig.defaults.amyboard`; the existing `partitions.csv` stays for the
8 MB custom profile (audio off).

---

## 4. HAL interfaces

```cpp
// hal/IAudioIo.hpp
struct AudioIoConfig { uint32_t sample_rate=44100; uint16_t block_frames=128;
                       uint8_t dma_desc=4; bool enable_input=false; };
class IAudioIo {
  virtual bool start(const AudioIoConfig&) = 0;
  virtual void stop() = 0;
  virtual bool write_block(const int16_t* interleaved) = 0;  // blocks on DMA space = pacing
  virtual bool read_block(int16_t* interleaved) = 0;         // non-blocking input
  virtual bool dma_mark(int64_t& t_us, uint64_t& frames_consumed) = 0;  // SampleClock feed
  virtual uint64_t frames_written() const = 0;
};

// hal/ILinkAudio.hpp — beats cross as Q32.32 int64; double↔Q32.32 conversion
// happens only in the core-0 ESP wrapper, keeping core 1 double-free.
struct AudioChannelInfo { char id[48]; char name[32];
                          uint32_t sample_rate; uint8_t num_channels; };
class ILinkAudio {
  static constexpr int kMaxSinks = 2;                        // master mix + line-in tap
  // Control plane (core 0 only):
  virtual int  sink_create(const char* name, uint32_t rate, uint8_t ch,
                           uint32_t max_block_frames) = 0;
  virtual void sink_destroy(int sink) = 0;
  virtual bool sink_has_subscribers(int sink) const = 0;
  virtual size_t channels(AudioChannelInfo* out, size_t cap) = 0;   // discovery
  virtual bool subscribe(const char* channel_id) = 0;               // one active source
  virtual void unsubscribe() = 0;
  // RT plane (audio task; lock-free ring push/pop):
  virtual void sink_write(int sink, const int16_t* interleaved, uint32_t frames,
                          int64_t begin_beat_q32, int64_t end_beat_q32) = 0;
  virtual uint32_t source_read(int16_t* interleaved, uint32_t max_frames,
                               uint32_t& sample_rate, uint8_t& channels,
                               int64_t& begin_beat_q32, int64_t& end_beat_q32) = 0;
  virtual uint32_t source_dropped() const = 0;
};
```

The `LinkAudioSource` receive callback (Link's network thread, core 0)
converts beats to Q32.32 and pushes into a PSRAM SPSC ring; `sink_write`
pushes to outbound rings drained by the pump task, which calls the real
`LinkAudioSink` write. No `captureAudioSessionState` is needed anywhere.

---

## 5. Audio engine architecture

```
core 0                                          core 1
link_svc (10ms) ──TimelineSnapshot──▶ [timeline_bus] ──▶ audio task (MAX-4), 2.9 ms loop
config_store ────AudioEngineConfig──▶ [audio cfg bus] ─▶   paced by write_block()
LinkAudio net thread ─▶ [rx ring] ─────────────────────▶   source_read
linkaudio pump (prio 11) ◀─ [tx rings] ◀─────────────────  sink_write
web/OLED ◀───AudioStatus seqlock ◀───────────────────────  status publish
```

Per block: read config+snapshot seqlocks (version-checked) → `dma_mark()` →
`SampleClock` update → compute the presentation **beat window** → render
sources → mix → sink taps → `write_block` → publish `AudioStatus`.

**Mixer graph** — per output channel (L/R) an `AudioRole`:
`kMix | kMetronome | kClock | kReset | kRun | kAmy | kLinkIn | kLineIn`.
Sources: ClickSynth, PulseRender (CLK/RST/RUN), AMY voice, LinkAudioSource,
line-in. Sinks: I2S out, LinkAudioSink "…Out" (mix tap), LinkAudioSink "…In"
(line-in tap). `kMix` = weighted sum of enabled sources. The pulse roles
deliver requirement 3 (1-sample placement).

**Beat↔sample mapping**: `SampleClock` maintains an int64 affine
`us_at_frame()` map with a slew-limited ppm rate term (first-order servo,
~0.05 Hz bandwidth) absorbing codec-crystal vs esp_timer drift. Block
presentation time `t_dac = us_at_frame(frames_written()) + kDacLatencyUs`;
beat via the snapshot's integer fields (existing `fixed_math.hpp` helpers).
This aligns audio pulses with the GPIO/CV path, which schedules against the
same esp_timer domain.

**Re-anchoring**: click/pulse rendering is stateless per block — recomputed
from the beat window, so a new snapshot re-anchors at the next block
(≤2.9 ms). Stateful guards: a sounding click envelope finishes; transport
stop or a phase jump > kBeatEpsilon gets a 1 ms fade (no pops).

**Receive path**: the jitter buffer targets
`read_beat = output_beat − jitter_ms→beats`; a fill-level servo trims the
resampler ratio ±500 ppm around `sender_rate/44100`. Underrun → silence +
counter; overrun → drop oldest. Ring ~250 ms @ 48 k stereo (~96 KB, PSRAM).
Writes into the ring are beat-addressed rather than appended: each block
lands at the position its begin beat implies, so a lost packet leaves a
silent hole at its exact timeline position, a reordered or duplicated
packet fills (or harmlessly overwrites) its own slot, and a beat jump too
large to be jitter — a loop wrap, a relocated playhead — re-anchors the
stream instead of being spliced.

---

## 6. Link 4.0 upgrade (isolated milestone)

1. Submodule `third_party/link` → tag **Link-4.0** (recursive; the asio
   submodule may bump past 1.36).
2. Re-diff `link_overrides/ableton/platforms/esp32/Context.hpp` vs upstream —
   delete the override if upstream absorbed the asio modernization, else
   refresh it.
3. Revalidate the asio defines in `components/ableton_link/CMakeLists.txt`
   (ASIO_SEPARATE_COMPILATION, `socketpair.h` force-include, `SA_*` shims) —
   expect churn here, not in Link proper.
4. `ableton::Link` → `ableton::LinkAudio` in `link_session_esp.cpp`;
   `ILinkSession` behavior is inherited, downstream untouched.
5. The stub leg builds zero Link-4 sources; CI stays green even if Link 4
   misbehaves on ESP32.
6. May need `CONFIG_LWIP_MAX_SOCKETS` 16→20 and lwIP mailbox bumps. **The PR
   ships with LinkAudio compiled but unused**; merge gate = timeline parity vs
   3.1.5 on hardware.

---

## 7. Config / REST / Web / OLED (10-step pattern, config v4)

Append to `neon::Config`, bump `kConfigVersion` 3→4 (`model.hpp`; sanitize in
`config_model.cpp`; JSON in `config_json.cpp`):

```cpp
enum class AudioRole : uint8_t { kMix=0, kMetronome, kClock, kReset, kRun,
                                 kAmy, kLinkIn, kLineIn };
enum class ClickSound : uint8_t { kSine=0, kNoise, kWood };
uint8_t audio_enabled=0, audio_role_l=0, audio_role_r=0;
uint8_t metro_enabled=0, metro_sound=0, metro_gain=200, metro_accent=1;
uint8_t amy_enabled=0, amy_gain=200, amy_patch=0;
uint8_t linein_monitor_gain=0;
uint8_t la_publish_mix=0, la_publish_linein=0, la_publish_mono=0, la_sub_gain=200;
uint8_t pad_audio_[1]={}; uint16_t la_jitter_ms=60;
char la_channel_name[24]="";      // "" = derive from device_name
char la_sub_channel_id[48]="";    // "" = not subscribed
```

v3 blobs migrate via the existing append-with-defaults mechanism. Live-apply:
gains/roles/metronome ride a new `AudioEngineConfig` seqlock slice;
restart-scope fields (audio_enabled, publish flags, subscription) are applied
core-0-side by audio_service/link_svc through the existing debounced apply
hook.

**REST** (`web_ui.cpp`): fields ride `PUT /api/config` partial-merge; add
`GET /api/audio/channels` (discovery list); `/api/status` gains
`audio:{running, underruns, peak_l, peak_r, publishing, subscribers,
sub_state, sub_rate}`.

**Web**: new `web/src/routes/Audio.tsx` (Output roles, Metronome, Synth,
Line In, Publish, Subscribe with channel picker + refresh, jitter slider,
meters), `nav.ts` entry, `api.ts` Config mirror, strings via
`scripts/gen_design.py` (never hand-edit `strings.ts`), `mock-device.mjs`
audio block + fake channel list, rebuild
`components/web_ui/www/dist/index.html.gz` — **mind the 60 kB budget**.

**OLED**: `menu_model.hpp` AUDIO submenu (Metronome on/off, Click vol/sound,
Out L/R role, Line-in mon, Publish, Subscribe cycling discovered channels),
item counts bumped, `design/screens.json` refreshed via
`host/sim/neon_screens`.

---

## 8. Host tests & CI

New test files (added to `host/CMakeLists.txt`):

| File | Asserts |
|---|---|
| `test_sample_clock.cpp` | Converges on +80 ppm synthetic drift to <1 sample; rejects ±200 µs mark jitter; slew limit; int64-only |
| `test_audio_click.cpp` | Exact click onset sample across tempos/block phases; accent on beat 1 only; deterministic waveforms; stop-fade to zero ≤1 ms |
| `test_pulse_audio.cpp` | Audio pulse edges within 1 sample of `MultiClockEngine` Edge times for identical snapshots; RESET/RUN semantics |
| `test_audio_mixer.cpp` | Role routing, gains, mono sum, soft-clip (no wrap), silence→silence |
| `test_frame_ring.cpp` | SPSC correctness under threads, wrap, drop-oldest, counters |
| `test_resampler.cpp` | 48 k→44.1 k sine frequency-correct; ratio servo converges; ratio-1.0 bit-exact |
| `test_jitter_buffer.cpp` | Beat-aligned read vs jitter target; buffering→playing states; underrun/recovery |
| extend `test_config*.cpp` | v3→v4 migration defaults; clamps; JSON round-trip; partial-merge isolation |

**CI**: add an `amyboard-audio` matrix leg (`sdkconfig.ci.audio`:
`NEON_AUDIO=y`, `NEON_AUDIO_AMY=y`, 16 MB partitions, app-size assertion);
keep the `stub` leg building with `NEON_AUDIO=y` + `NEON_LINK_STUB=y` +
`NEON_AUDIO_AMY=n` (proves both stubs).

---

## 9. Milestones (one PR each; ⚑ = feasibility gate)

1. **PR1 — Audio out foundation**: I2S pins from the AMYboard schematic into
   `board_pins.h` (+MCLK question), `IAudioIo` + `i2s_audio.cpp`,
   `NEON_AUDIO` Kconfig, `audio_service` skeleton (core 1, test tone),
   `dma_mark`, `SampleClock` + `frame_ring` + tests, `partitions_16mb.csv`.
   ⚑ scope CLK1 jitter unchanged with audio running; SampleClock residual on
   hardware.
2. **PR2 — Metronome + pulses-as-audio**: click/pulse_render/mixer/
   beat_window, config v4 + full 10-step integration, AudioStatus bus, tests.
   First user-visible feature.
3. **PR3 — Audio in**: duplex I2S, line-in monitor role/gain, input meters.
   ⚑ PCM1808 MCLK works as wired.
4. **PR4 — Link 4.0 upgrade**: as §6; zero feature change; gate = timeline
   parity + legacy CI legs green. ⚑ LinkAudio compiles for ESP32 — if not,
   PR5/6 park while 1–3, 7 proceed.
5. **PR5 — Link Audio publish**: `ILinkAudio` + ESP impl + pump task + stub,
   mix/line-in sinks, publish UI. ⚑ sustained 1.4 Mbps to a Live 12.4
   subscriber without link_svc starvation; else mono default + "disable BLE
   while streaming" note.
6. **PR6 — Link Audio subscribe**: discovery REST/UI/OLED, jitter_buffer +
   resampler wired, `kLinkIn` role live.
7. **PR7 — AMY voice**: `third_party/amy` submodule, `amy_synth` + stub,
   MIDI router extension (BLE/TRS notes → AMY), patch/gain config. ⚑ N-voice
   render ≤50% of the block budget at MAX-4 without cv_mirror jitter
   regression; clamp default voices to what passes.
8. **PR8 — Polish**: latency calibration vs CV outs, docs updates
   (`ARCHITECTURE.md`, `AMYBOARD.md`, S/PDIF-deferred, amend the two "out of
   scope" lines in `HARDWARE.md`/`SOFTWARE.md`), OTA size assertion, bundle
   budget audit.

---

## 10. Risks & mitigations

| Risk | L/I | Mitigation |
|---|---|---|
| Link Audio doesn't build/run on ESP32 | M–H / H | PR4 is an isolated gate; the stub `ILinkAudio` keeps PRs 1–3, 7 shippable; the overrides dir is the patch point; worst case: 4.0 timeline-only, park PR5/6 |
| WiFi jitter/bandwidth (AP mode, BLE coexist) | M / M | 60 ms jitter buffer, mono publish option, drop counters in status, "BLE off for streaming" doc, lwIP socket bump |
| I2S pins/MCLK unknown | H / L | First PR1 task; `-1` sentinels keep other profiles compiling; fallback = input-disabled build |
| Audio task disturbs the pulse path | L / H | MAX-4 < cv_mirror < pulse; DMA slack tolerates preemption; PR1 scope gate before any DSP lands |
| AMY CPU blows the block budget | M / M | Voice clamp from the PR7 gate; fully optional (Kconfig + runtime) |
| PSRAM bandwidth contention | L / M | Rings ~150 KB; DMA buffers internal; move the rx ring internal at reduced depth if profiling shows stalls |
| 3 MB OTA slots too small | H / L | `partitions_16mb.csv` (6 MB slots) in PR1 + CI size assertion |
| Web bundle > 60 kB | M / L | Single Audio route, reuse controls; trim strings before inventing chunking |
| Doubles creep into core-1 audio | M / M | Q32.32 across `ILinkAudio`; conversion only in the core-0 wrapper; host tests |
| S/PDIF expectations | — | Explicitly deferred, documented in PR8 |

---

## 11. Verification

- **Host**: `cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build build-host -j && ctest --test-dir build-host
  --output-on-failure` — all new DSP/logic tests run under ASan/UBSan.
- **Firmware CI**: ESP-IDF v5.3.2 matrix `[link, stub, bleoff,
  amyboard-audio]` all green; `scripts/gen_design.py --check`, screens drift
  check, bundle freshness/budget.
- **Hardware gates** (flagged per PR): scope CLK1 jitter with audio running;
  test tone → metronome audible against Live's click; Live 12.4 subscribes to
  "NEON LINK Out" and audio arrives in sync; the module subscribes to a Live
  channel and plays it; AMY voice under MIDI input while streaming.
- **End-to-end acceptance**: module + Live 12.4 on one WiFi network —
  tempo/phase/transport locked, metronome sample-aligned with Live's click,
  mix published and audible in Live, a Live channel audible from the module,
  CV/MIDI outputs unaffected throughout.

---

## 12. Implementation status

The design above is what was built; this section is what a reader needs in
order to know where the edges are. Deviations from §3/§7 are noted where
they exist — there are two, both structural rather than behavioural.

### Shipped

| Area | Where |
|---|---|
| `SampleClock`, `beat_window`, `ClickSynth`, `PulseRender`, `Mixer`, `FrameRing`/`AudioBlockRing`, `LinearResampler`, `JitterBuffer`, `SynthVoiceBank` | `components/neon_core/{include/neon,src}/audio/` |
| `IAudioIo`, `ILinkAudio` | `components/neon_hal/include/hal/` |
| I2S TX driver (PCM3060 32-bit slots, ISR DMA marks) | `components/neon_hal_esp/src/i2s_audio.cpp` |
| Render loop (core 1) + control task (core 0) + discovery JSON | `main/audio_service.cpp` |
| Link Audio impl + pump task, and the no-op that stands in for it | `components/ableton_link/src/link_audio_{esp,stub}.cpp` |
| Synth seam (AMY or the built-in voice) | `components/amy_synth/` |
| Config v4, REST, web Audio route, OLED AUDIO screen | see §7 |
| 16 MB partitions, `NEON_AUDIO*` Kconfig, CI legs | `partitions_16mb.csv`, `main/Kconfig.projbuild`, `.github/workflows/ci.yml` |

Host tests: `test_sample_clock`, `test_audio_click`, `test_pulse_audio`,
`test_audio_mixer`, `test_frame_ring`, `test_resampler`,
`test_jitter_buffer`, `test_synth_voice`, plus the v3→v4 migration, clamp,
JSON round-trip and partial-merge cases in `test_config*` and the AUDIO
screen cases in `test_ui`.

### Two deviations from §3/§7

1. **The config fields are a nested `AudioConfig`, not flat members.** §7
   lists them flat. Nested makes the v3→v4 migration a single assignment,
   and it has to be one: a v3 payload's size ran into its own tail padding,
   which would otherwise be decoded as audio configuration. That is the
   exact bug the v2→v3 migration already had to special-case. The JSON is
   an `"audio"` object either way.
2. **`AudioRole` / `ClickSound` live in `neon/audio/types.hpp`,** not in
   `config/model.hpp`, so the DSP headers do not pull the configuration
   model in. `model.hpp` includes them, so §7's code reads the same.
3. **48 kHz, not 44.1; PCM3060, not PCM5101/PCM1808.** The LINE codec is
   the AMYboard's PCM3060 (I2S slave, 32-bit left-justified slots). Live's
   default rate is 48 kHz; locking the DAC there and resampling the
   incoming stream is what actually held fill on hardware. The I2S pin
   map is the tulip/amyboard one (MCLK=3 BCLK=8 WS=2 DOUT=6 DIN=9) and
   is the AMYBOARD Kconfig default. Duplex is off: the RX DMA ring OOM'd
   the S3 when WiFi associated.
4. **`ILinkAudio` talks to the real Link-4.0 API.** Channel ids are the
   16-hex `ChannelId`, sinks take a `LinkAudio&`, and a `BufferHandle`
   is only valid while someone is subscribed. `third_party/link` is
   pinned at `Link-4.0`. The Link asio task runs at priority 8 / 16 kB
   (priority 2 lost packets to HTTP/OLED). `ScanIpIfAddrs` hides the
   SoftAP when STA has a LAN address so Live can reach the unicast port.

### Not in this tree

- **AMY.** `third_party/amy` is not vendored. `CONFIG_NEON_AUDIO_AMY` is
  off by default; `components/amy_synth/src/amy_synth.cpp` is written
  against AMY's API and is not compiled until the submodule is added. Until
  then the same seam is served by `neon::SynthVoiceBank`, which is a real
  voice — the Synth role, the MIDI routing and the gain control are all
  exercised by it, so adding AMY changes the timbre and nothing else.

### Hardware gates still outstanding

Everything here is unverifiable without the board, and none of it is
claimed:

- Scope CLK1 jitter with audio running, and confirm the pulse path is
  unchanged (PR1 gate).
- `SampleClock` residual on real hardware; calibrate `kDacLatencyUs` in
  `main/audio_service.cpp` against the CV outputs (currently 0).
- PCM3060 input path — duplex is compiled but off; if it is turned on,
  confirm the heap still has room once WiFi is up.
- Sustained publish to a Live 12.4 subscriber without starving `link_svc`;
  if it does starve, the mono publish option and the "BLE off while
  streaming" note are the mitigations already in place.
- AMY's voice count against the 5.3 ms block budget.
