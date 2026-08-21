# PRD + SPEC — Stem Sync

**Working name:** Stem Sync
**Status:** proposal. Nothing committed until the spikes in §7 come back.
**Supersedes:** Link Audio as the multi-player audio strategy.

---

## 1. Why this exists

Link Audio was cut after four attempts and a measurement that closed it: a
105 ms tail on an idle one-station SoftAP with nothing else on the network.
Wireless PCM on this platform is not a transport, and no amount of buffering
or radio choice fixes a delay budget that starts above the target.

Stem Sync makes the same move Ableton Link itself makes: **distribute the
expensive thing ahead of time, send events at play time.**

Musicians preparing a set have hours. The content is pre-recorded. The
targets have SD slots and idle CPU. So ship the audio during load-in and send
only "scene N starts at beat B" during the show.

**Result:** zero audio on the wire at play time, no congestion, no jitter
buffer, no concealment, no drift servo racing a deadline.

---

## 2. The user and the job

**Primary:** a band or producer playing a prepared set, where one person runs
Ableton Live and the others do not.

**Job to be done:** every player hears the backing content, locked to the same
timeline, **in their own monitor mix**, without a laptop each and without a
wired snake.

**The competitor is not Ableton.** It is a $200-per-player wired personal
monitor mixer. Stem Sync undercuts it on price and cabling and adds tempo
sync it does not have.

**Honest scoping:** a laptop can do all of this. Nobody will put four laptops
in front of four musicians to do it. The product is viability, not
capability.

---

## 3. The headline feature: per-player mixes

If every client plays the same stereo bounce, a laptop already does that.

**Ship stems, mix locally.** The bassist wants more click. The singer wants
more keys. The drummer wants no pads. That is impossible from one shared
stream and it is the thing bands actually argue about at rehearsal.

It is also the "everyone in the mix" principle taken to its conclusion: not
one cue mix distributed, but **N cue mixes generated locally from the same
content.**

---

## 4. Architecture

```
  PREPARE (hours before)          PLAY (on stage)
  ─────────────────────           ────────────────
  Live session                    Live session
      │                               │
   export stems                  scene launch
      │                               │
   naming convention             {scene, start_beat}
      │                            broadcast
   transfer to clients                │
      │                               ▼
   SD card index                 each client schedules
                                 locally vs Link timeline
                                       │
                                  local stem mix → DAC
```

### 4.1 Launch events are beat-scheduled, not immediate

**Do not use MIDI Program Change.** One byte, no timestamp; arrive 50 ms late
and the launch is already missed.

Ableton scene launches are **quantized** — usually one bar. At the moment of
the click, Live already knows the target beat. So send:

```json
{ "type": "launch", "scene": 7, "start_beat_q32": <int64>, "quantum": 4 }
```

Each client schedules against its own Link timeline. **A late packet is
harmless as long as it arrives before the target beat.** With one bar of
quantization at 120 BPM that is 2000 ms of slack against a 105 ms tail — a
19× margin.

This is the same refill-horizon pattern already proven in the pulse path.

### 4.2 Transport is unicast UDP, repeated

Send each launch event **3–5 times over ~200 ms**. Idempotent by
`(scene, start_beat_q32)` — a client that already has it discards duplicates.
No retransmit logic, no acknowledgement, no head-of-line blocking.

Cost: a few hundred bytes per scene change. Compare 176 KB/s for one PCM
stream.

### 4.3 Drift becomes trivial

Each client plays from its own DAC clock. At 40 ppm that is ~12 ms over a
five-minute song — **the same drift as Link Audio.**

The difference is that the jitter buffer is now the entire file. Underrun is
impossible. There is no deadline. A 12 ms correction can be spread over
minutes via `LinearResampler` trim, which at that rate is 0.07 cents of pitch
shift — roughly a hundredth of audibility.

**Every mechanism built for Link Audio survives. Every failure mode it was
fighting disappears.**

### 4.4 Network loss is survivable

The file is local. A client that loses WiFi mid-song keeps playing from its
last known beat and re-syncs when Link returns. Graceful degradation for
free — which Link Audio never had.

---

## 5. Content pipeline

### 5.1 V1: convention, not automation

**There is no Live API for "export scene 3."** Freeze/Flatten and Export
Audio are not scriptable in the way this would need.

So v1 is: the user exports however they like, into a folder, following a
naming convention.

```
set-name/
  manifest.json
  01-intro/
    drums.wav
    bass.wav
    keys.wav
    click.wav
  02-verse/
    ...
```

The tool handles **transfer, validation, indexing, and SD layout** — which is
genuinely useful and is not a research project.

### 5.2 Format

**WAV only. 48 kHz / 16-bit.** No MP3.

- Decode costs CPU for no benefit — SD bandwidth is not the constraint
- 4 stems × 192 KB/s = 768 KB/s, trivial for SD
- Seeking in compressed formats is a problem you do not need

Mono stems where the source is mono; halves the read.

### 5.3 Manifest

```json
{
  "set": "friday-set",
  "sample_rate": 48000,
  "scenes": [
    { "index": 1, "name": "intro", "bars": 8, "bpm": 120,
      "stems": [ {"file": "01-intro/drums.wav", "gain_db": 0.0} ] }
  ]
}
```

`bars` and `bpm` let a client verify a stem's length matches its musical
duration — catching the export-at-wrong-tempo mistake before the show
rather than during it.

### 5.4 Transfer

- **SD card, sneakernet** — v1, always works, no code
- **HTTP upload to the module** — v1.1, over the existing web server
- Wireless transfer happens at load-in, never during play

---

## 6. Client playback

### 6.1 Per-client mix

Local mixer, one gain per stem, saved per device. Set from the web UI or a
front-panel encoder. `render_mix` already does the summing and returns a gain
bound.

### 6.2 Scheduling

On `launch` for beat B:
1. Open and seek stems, prime the read-ahead
2. Compute frame offset for B via `beat_window` / `SampleClock`
3. Start at that frame

Read-ahead from SD must be deep enough to cover the **22.4 ms flash stall
measured in A1** — the SD and flash controllers contend. Size at 2–3×, same
as the G7 refill horizon.

### 6.3 Follow, don't free-run

Once playing, continuously compare playback frame against the Link beat and
correct with resampler trim. Never seek to correct — that is audible. The
servo has minutes; use them.

---

## 7. Spikes — before committing

### S1. Scene state out of Live — **the critical unknown**

**A VST cannot do this.** Plugins are sandboxed: they see audio and MIDI on
their own track, not the session grid.

Reading `playing_slot_index` needs the **Live Object Model**, which means a
Max for Live device or a **Control Surface script**. Control Surface is how
Push works and has full LOM access.

**Question:** can a Control Surface script observe scene launch and the
quantized target beat, and get that out over UDP, across Live 11 and 12?

This is the same class of problem flagged as critical in Posttape. **If this
fails, the whole product needs a different trigger source** — footswitch, a
dedicated "conductor" client, or manual scene advance.

Do this first. It can kill the design.

### S2. SD read throughput under WiFi load

4 stems × 192 KB/s while the radio is up and Link is running. Read-ahead
depth needed to survive the A1 flash stall.

### S3. Export tempo verification

Does a naive Live export land exactly on bar boundaries at the session tempo?
Off-by-a-few-samples per scene accumulates into an obvious problem.

---

## 8. Scope

### V1 in

- Manifest-driven stem playback from SD
- Beat-scheduled launch over UDP, repeated
- Per-client stem mix, persisted
- Link timeline follow with resampler trim
- Graceful behaviour on network loss
- Desktop tool: validate, index, transfer

### V1 out

- Automated bounce from Live (no API — §5.1)
- Per-clip (vs per-scene) launch — same mechanism, more content, later
- Wireless transfer — v1.1
- MP3 or any compressed format
- Any audio on the wire at play time, ever

### Done means

1. Four clients, same set, launch scene 3 in Live → all four start on the
   correct beat, within one audio block of each other
2. 30-minute set, no audible drift
3. Client power-cycled mid-set rejoins and lands correctly on next launch
4. WiFi pulled for 60 s → playback continues, re-syncs on return
5. Each client's mix is independently set and persists across reboot

Item 4 is the one that proves the architecture.

---

## 9. Why this succeeds where Link Audio failed

| | Link Audio | Stem Sync |
|---|---|---|
| Wire at play time | 176 KB/s PCM | ~200 bytes per scene |
| Deadline | every 5.8 ms block | one bar of slack |
| Underrun | silence, full re-buffer | impossible |
| Drift correction time | milliseconds | minutes |
| Network loss | dropout | keeps playing |
| Per-player mixes | no | **yes** |
| Concealment needed | 120 Hz–5 kHz filter | none |

The measurement that killed Link Audio — a 105 ms idle tail — is a **19×
margin** here.

---

## Appendix — status against the tree (2026-08-21)

Added when this PRD landed in the repo. Everything above is the proposal
as written; this maps it onto what the firmware already has. The Link
Audio path itself ([AUDIOLINK.md](AUDIOLINK.md)) stays in the tree — its
mechanisms are exactly what §4.3 says survive.

### The measurements cited are real and in the tree

| PRD claim | Source |
|---|---|
| §1 "105 ms tail on an idle one-station SoftAP" | [STUDIO_MODE_RESULTS.md](STUDIO_MODE_RESULTS.md) B-AP rerun: 100-ping idle RTT max **105.573 ms** (avg 16.1 ms, 32.2× max/min), zero loss, no other traffic |
| §6.2 "22.4 ms flash stall measured in A1" | `tools/a1_psram_stall` S3 rev2: `stall_max_us=22384`. Documented as a lower bound twice ([FRIENDS_FAMILY_HANDOFF.md](FRIENDS_FAMILY_HANDOFF.md) G7) |
| §4.1 "refill-horizon pattern already proven in the pulse path" | `main/tasks_core1.cpp` — pulse refill horizon is 67 ms = 3× the A1 stall (ship gate G7, landed) |

Two corrections. First, the §4.1 / §9 "19× margin" is the **maximum**,
not the typical: the real margin is `target_beat_time − moment_of_click`,
and musicians launch scenes on or just before the beat — the worst case
is the normal case. Measuring that distribution is the point of the S1
spike (`tools/s1_scene_launch`), and the §6.2 seek-into-file path is the
mitigation when a launch event arrives after the target beat.

Second, 22.4 ms is the **S3** number.
[P4DEVKIT.md](P4DEVKIT.md) measured **51.0 ms** for the same stall on
ESP32-P4, so on P4 targets the §6.2 sizing rule (2–3×) means
~100–150 ms of read-ahead — roughly 10–15 KB per mono 48 kHz/16-bit
stem, still trivial in PSRAM.

### Already shipped — the mechanisms §4.3 says survive

| PRD item | Where |
|---|---|
| §4.3 / §6.3 resampler trim | `components/neon_core/include/neon/audio/resampler.hpp` — `LinearResampler`, Q32.32 step with `set_trim_ppm` (±500 ppm ceiling). The 40 ppm crystal case uses 8 % of the trim range |
| §6.3 follow servo pattern | `JitterBuffer::update_servo` already steers that trim from buffer fill level. Stem Sync re-points the same servo at (playback frame − Link beat) error — with minutes of correction time instead of milliseconds |
| §6.2 scheduling math | `components/neon_core/include/neon/audio/beat_window.hpp` — `us_at_beat_q32`, `BeatWindow::frame_of_beat`; `SampleClock` maps DAC frames to µs. Beats are already signed Q32.32 `int64` — the `start_beat_q32` field in the §4.1 payload is the tree's native beat type, no conversion |
| §4.1 quantum awareness | `TimelineSnapshot.quantum_beats` already crosses to the audio task via `SeqLock` |
| §5.2 48 kHz | The audio path already runs at 48 kHz (`main/audio_service.cpp`, `kSampleRate`) |
| §6.1 mix + gain bound | `render_mix` (`components/neon_core/src/audio/mixer.cpp`) sums with a proven gain bound and skips the saturator when the mix cannot clip; gains are byte-quantized and persisted in NVS; web UI infrastructure exists (`components/web_ui`, `web/`) |
| §5.4 HTTP upload host (v1.1) | The module's HTTP server (`components/web_ui`) already serves the REST API and web app — an upload endpoint extends it, not a new server |
| §4.4 rejoin behaviour | `main/wifi.cpp` retries stored networks forever; Link re-converges on rejoin. The *audio* half of graceful degradation is new, but the network half exists |

### The new work

- **SD playback is greenfield.** There is no SDMMC/FatFS mount, no VFS
  use, and no WAV reader anywhere in the tree — `SOC_SDMMC_*` flags in
  `sdkconfig.p4v31` are chip capabilities, not wired code, and
  [FEATURES.md](FEATURES.md) still lists SD storage under deferred.
  Mount, manifest parse, WAV parse, and the multi-stem read-ahead task
  are all new. The A1/G6 discipline applies: NVS commits are already
  held while I2S runs (`neon_config_hold_nvs`), and SD reads need the
  same treatment as a flash-stall source until S2 says otherwise.
- **Mixer generalization.** `MixSources` is a fixed role set (metro,
  synth, Link-in, line-in). N file-backed stems with per-stem persisted
  gains needs a stem source array, a config schema, and a web UI page —
  mechanical, but real.
- **Launch transport.** No general-purpose UDP message channel exists;
  `main/link_service.cpp` speaks Link only. The repeated idempotent
  launch datagram is new on both the sender and the module.
- **S1 sender.** The tree's own plugin confirms the constraint the spike
  names: `plugin/README.md` — VST3, HTTP config manager, "not a Link
  peer", no session-grid access. No Control Surface script or Max for
  Live device exists in the tree. S1 stays the critical unknown and
  nothing here pre-empts it. Phase A of the spike — measuring the
  launch-lookahead distribution via AbletonOSC, no code of our own in
  Live — is tooled at `tools/s1_scene_launch` (daemon, analysis,
  runbook); it produces the go/no-go before any Phase B work.
- **Desktop tool.** Nothing exists for validate/index/transfer.

### Hardware caveat — "the targets have SD slots"

Not established in this repo. SDMMC host support appears only in the
ESP32-P4 configs; the AMYboard/S3 targets declare no SD interface in
[HARDWARE.md](../HARDWARE.md) or any board pin map. Either the S3 targets
get SPI-mode SD on the expansion header, sets live in spare flash
(16 MB parts, ~2 songs of 4 mono stems per available 8 MB — tight), or
v1 scopes stem playback to P4-class hardware. Needs a hardware answer
before S2 is even runnable on S3.

### Reference not in this tree

§7 S1 cites "Posttape" — that PRD is not in this repo; the citation is
kept as written.
