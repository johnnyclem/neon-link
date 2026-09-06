# Audio Metronome Output and Audio-In Tempo Follow

**Author**: (draft)  
**Date**: 2026-09-06  
**Status**: Draft (rev 3 — review)  
**Config version**: 13 → 14  
**Related**: [`docs/AUDIOLINK.md`](docs/AUDIOLINK.md) · [`docs/SPIKE_MIDI_PLL.md`](docs/SPIKE_MIDI_PLL.md) · [`components/neon_core/include/neon/clock_arbitration.hpp`](components/neon_core/include/neon/clock_arbitration.hpp)

---

## Overview

NEON LINK already renders a sample-accurate metronome (`neon::ClickSynth`) into the I2S mix, and already follows analog CLK IN and MIDI clock into Ableton Link via a single arbitration contract. Two gaps remain.

**Part A** is not a new audio engine. It is an audit of which targets actually emit click, which have I2S/codec/speaker hardware that the firmware leaves idle, and the smallest set of pin/codec/menu changes so “click out when the hardware can” is true. `metro_enabled` stays **0**. Nothing starts clicking because a board gained a speaker.

**Part B** is the new work: a cheap time-domain onset detector on the I2S input whose timestamps drive Link tempo the way a drummer’s tap-tempo would — **tempo only, no phase re-anchor** — so a live kit that drifts a few BPM over a song can pull the session with it. The detector lives in `neon_core` and is host-tested. The audio task (core 1) never calls Link. Publish policy, lock-range, and clock-source arbitration reuse the CLK IN / MIDI follower patterns rather than inventing a parallel clock path. The feature is **off by default** and must be enabled in settings.

---

## Background & Motivation

### Current state — audio / metronome (Part A)

The AudioLink engine shipped. Per-block click, pulse-as-audio, mixer, SampleClock, and the core-1 render loop are live:

| Piece | Where |
|---|---|
| `ClickSynth` (sine/noise/wood, accent, `follow_transport`) | `components/neon_core/include/neon/audio/click.hpp` |
| Render loop, 48 kHz / 256 frames, `read_block()` already called | `main/audio_service.cpp` |
| Duplex I2S seam | `hal/IAudioIo.hpp`, `halesp/i2s_audio.cpp` |
| ES8311 + PA | `halesp/es8311.hpp` — **compiled for `CONFIG_NEON_BOARD_P4DEVKIT` only** |
| `AudioConfig` (`enabled`, `metro_enabled`, `metro_sound`, `metro_gain`, `metro_accent`, `linein_monitor_gain`, …) | `components/neon_core/include/neon/config/model.hpp` |
| OLED AUDIO: AUDIO / METRO / CLICK / SOUND / OUT L / OUT R / LINE IN / PUBLISH / SUB | `MenuModel::kAudioItems = 9` |
| Web Audio route | `web/src/routes/Audio.tsx` |
| I2S pin sentinels | `board_pins.h` + `NEON_I2S_*` Kconfig |

`metro_enabled` defaults to 0. `audio.enabled` defaults to 0. I2S stays down until `AudioEngineConfig::i2s_needed` is set (`audio_engine_config()` in `config_model.cpp`: engine on, or Link Audio pub/sub). That G6 rule is correct and stays.

What is **not** true today is “every board with a speaker or DAC can click.” Several targets have the silicon and the pin map and still never start I2S, or start I2S without programming the codec / raising PA.

### Current state — tempo follow (Part B)

Three follow paths already exist. Audio-follow must slot into them, not bypass them.

1. **`TapTempo`** (`transport.hpp` / `transport.cpp`) — interval average of a run of taps, 3 s timeout, absurd-interval rejection. `link_service.cpp` applies a tap as `session.set_tempo(...)` **only**. No `request_beat_at_time`. The product owner’s analogue.

2. **`ExtClockEstimator`** (`ext_clock.hpp`) — CLK IN period → 0.5×–2× outlier gate → median-of-5 → EMA α=1/8 → milli-BPM. Publish at 0.5 % hysteresis (`kHysteresisDen = 200`) after `kSettlePulses = 3`. Phase only on RST IN.

3. **`MidiClockPll` + `midi::SyncFollower`** — type-II PI on 24 PPQN ticks, integer-BPM Schmitt (0.6 BPM, min 1 s), Link-authority policy. Phase is a property of the MIDI stream (Start = beat 0). See `docs/SPIKE_MIDI_PLL.md`.

Arbitration is one function, host-tested:

```22:31:components/neon_core/include/neon/clock_arbitration.hpp
constexpr ClockArbitration arbitrate_clock_source(ClockSource source,
                                                  bool clk_in_active) {
  ClockArbitration a;
  a.follow_clk_in = (source == ClockSource::kAuto ||
                     source == ClockSource::kExternalMaster) &&
                    clk_in_active;
  a.midi_allowed = (source == ClockSource::kAuto ||
                    source == ClockSource::kMidiMaster) &&
                   !a.follow_clk_in;
  return a;
}
```

Today: **CLK IN > MIDI clock > session** under `kAuto`. `kLinkMaster` ignores both. Audio-follow must join this table.

### Pain points

- A drummer on a neon-link session drifts a few BPM across a song. The only legal way to pull the session is tap-tempo on glass / encoder. Hands are on sticks.
- Several boards can already click (P4 LCD speakers, AMYboard LINE, P4 v3.1 jack) but the setting is easy to miss, and boards with ES8311/ES7210 (RLCD) or Tab5 speakers never bring the path up.
- Turning on a metronome next to a follow-mic is a classic feedback lock. The feature must not surprise a live session, and must not chase its own click.

---

## Goals & Non-Goals

### Goals

- Click reaches every target whose pin map / codec / speaker amp is already identified in-tree, still **off by default**.
- I2S line-in or onboard mic can drive session tempo via transient detection, **off by default**, enabled in AUDIO settings.
- Behavior matches tap-tempo: `setTempo` proposals only. No phase re-anchor unless a separate setting, also off, is turned on.
- Detector + follower are portable `neon_core`, float32 / integer, host-tested with synthetic kick/click trains (steady, drift, dropout, double-hits, octave error, metronome self-bleed).
- Audio task detects; `link_service` (core 0) owns `setTempo`. Pulse ISR is untouched.
- Config v13 blobs migrate: new fields default off.
- Publish policy does not tenths-chatter at Live or fight a peer.

### Non-Goals

- FFT / autocorrelation beat tracking, downbeat-of-the-song detection, genre classifiers, or “listen and find the tempo from silence.”
- Replacing CLK IN or MIDI clock. Those stay higher priority.
- Making the metronome on by default, or auto-enabling audio on boards that have speakers.
- Link Audio streaming changes.
- Inventing a second SampleClock, a second control queue, or calling Ableton Link from core 1.
- Phase-locking the pulse grid to kick transients in v1 (optional setting only).
- Supporting boards with no ADC as follow sources (hide the row; honest-menu).
- Guessing Tab5 I2S pin numbers. That is a hardware bring-up task, not this design.

---

## Proposed Design

### Part A — Metronome output: inventory and gap-fill

#### Target inventory

| Target | `CONFIG_NEON_AUDIO` | I2S map | Transducer | Input | Click today | Gap |
|---|---|---|---|---|---|---|
| **AMYboard** | y | MCLK 3, BCLK 8, LRCLK 2, DOUT 6, DIN 9 | PCM3060 LINE jack | DIN wired; `NEON_AUDIO_INPUT` n (duplex heap) | Yes, if AUDIO+METRO on | Input off. Click path complete. Discoverability only. |
| **P4 v3.1 (`p4v31`)** | y | MCLK 13, BCLK 12, LRCLK 10, DOUT 9, DIN 11, PA 53 | ES8311 + NS4150B 3.5 mm | DIN wired; ADC regs not programmed | Yes | ADC init if follow is used. |
| **P4-DEV-KIT (`p4devkit`)** | **n** (pins present) | same as v3.1 | same | same | Silent | Turn audio on only when someone wants the jack; not a default-on change. Document. |
| **CrowPanel P4 LCD** | y | MCLK −1, BCLK 22, LRCLK 21, DOUT 23, DIN −1 | Dual NS4168 2×3 W speakers | none | **Yes** (`docs/LINKSYNC_P4LCD.md` §Speakers) | No ADC → hide follow. Click complete. |
| **RLCD 4.2"** | **n** | Kconfig: MCLK 16, BCLK 9, WS 45, DOUT 8, **DIN default −1** (vendor DIN 10 is a comment in `board_pins.h` / `docs/LINKSYNC_RLCD.md` only), **PA 46** | ES8311 DAC + ES7210 ADC, speaker amp | DIN not in Kconfig yet | Silent | **Largest click gap.** `kPinI2sPa = 46` exists; `es8311_start()` **and** the `I2sAudio::start/stop` call sites (`i2s_audio.cpp:190`, `:204`) are `#if CONFIG_NEON_BOARD_P4DEVKIT` only. Extending the stub without the call site is a no-op. |
| **Tab5** | n | **not in tree** | Speakers + mics (vendor codec, typically ES8388-class) | likely | Silent | Hardware bring-up: pin map + codec. Do not invent GPIOs. |
| **Teensy 4.1** | always (Audio lib) | SGTL5000, 44.1 kHz / 128 | line/headphone | unused (`NeonAudioSource` is 0-input) | Yes | Wire `AudioInputI2S` for follow. |
| **Daisy Seed** | always (libDaisy) | onboard codec, 48 kHz / 48 | line/headphone | `in` ignored (`(void)in`) | Yes | Use the existing input buffer for follow. |
| **MaTouch, JC3248, C3 OLED, EPD, XIAO link-sync** | n | all −1 | none | n/a | n/a | Out of scope. Honest-menu already hides AUDIO on MaTouch. |

`audio_service.cpp` already handles “pins are −1”: it software-paces so Link Audio can still stream. Click on those boards is a no-op, which is correct.

#### Smallest Part A change set

1. **RLCD: turn the existing map on.** Three independent holes, all required:
   - `sdkconfig.defaults.linksync-rlcd`: `CONFIG_NEON_AUDIO=y`. Do **not** default `metro_enabled` or `audio.enabled`.
   - `main/Kconfig.projbuild` `NEON_I2S_DIN`: add `default 10 if NEON_BOARD_LINKSYNC_RLCD` (today only AMYBOARD=9, P4DEVKIT=11, else **−1**). Mirror `CONFIG_NEON_I2S_DIN=10` in the sdkconfig defaults. Without this, FOLLOW stays hidden on the one LinkSync board that has a mic.
   - Compile **and call** ES8311: change `#if CONFIG_NEON_BOARD_P4DEVKIT` in **both** `es8311.cpp` (the real vs stub split) **and** `i2s_audio.cpp` `start()`/`stop()` to `P4DEVKIT \|\| LINKSYNC_RLCD`. I2C 13/14 is already brought up in `app_main.cpp`. PA pin is already 46.

2. **P4 LCD: no code.** Speakers already click when AUDIO+METRO are on. Confirm the AUDIO tab stays visible (it does).

3. **AMYboard / P4 v3.1: no click-path code.** Engine is live. Part A here is UI copy: “Click is off until AUDIO and METRONOME are both on.”

4. **PA enable is not a separate HAL.** `es8311_start()` already raises `kPinI2sPa`. P4 LCD NS4168s have no mute GPIO in-tree; they play whatever I2S carries. Keep it that way.

5. **Default routing.** `role_l` / `role_r` stay `kMix`. Metronome is in the mix when `metro_enabled`. Do not auto-switch a jack to `kMetronome`. Speaker boards (P4 LCD, RLCD) already hear the mix.

6. **Discoverability.** Add one sentence to the web Audio metronome card and the OLED AUDIO screen note: click is opt-in. Do not add a first-boot wizard. Do not change `follow_transport` (LinkSync units gate click on transport — `audio_service.cpp` `#if CONFIG_NEON_LINKSYNC` — that is already the right speaker behavior).

7. **Tab5.** Separate bring-up PR after pins are confirmed on hardware. Until then the engine is off (`CONFIG_NEON_AUDIO` unset). The AUDIO **tab is not hidden** — Tab5 shares `lcd_service.cpp` with P4 LCD, and `kTabs` always includes `Screen::kAudio`. PR2’s FOLLOW vis-map, keyed off `DIN < 0`, will hide FOLLOW / F SENS / F PHASE on Tab5 the same way as P4 LCD; it will not hide the AUDIO tab. Do not invent I2S pins.

8. **`p4devkit` rev v1.3.** Leave `CONFIG_NEON_AUDIO` unset (comment in `sdkconfig.defaults.p4devkit` is explicit). Operators who want the jack use the v3.1 overlay.

Part A does not touch `ClickSynth`, the mixer, or the render graph.

---

### Part B — Audio-in tempo follow

#### Architecture

```
core 1 (audio task, MAX-4)                  core 0 (link_svc, 10 ms)
─────────────────────────────               ──────────────────────────
ClickSynth::render  (needed for last_onsets)
note_click iff enabled && metro && follow
read_block() int16 stereo
int16_to_float → L/R
OnsetDetector::process(L, R, n,
  t0_us, us_per_frame_q32)
      │
      │ neon::OnsetEvent { t_us, strength }
      ▼
onset_queue                                drain onset_queue
 ESP: xQueue depth 16                      AudioTempoFollower::on_onset
 T41/Daisy: IrqRing<OnsetEvent,16>         (subdivision picker +
 drop **new** on overflow                    ExtClockEstimator)
                                           │
                                           ▼
                              arbitrate_clock_source(...,
                                  clk_in, audio_follow_enabled)
                                           │
                                           │ audio_allowed && !midi following
                                           ▼
                              session.set_tempo(bpm)   // tempo only
                              (phase, if enabled, is
                               decided in link_service)
                                           │
                              FollowStatus seqlock ──► OLED / web / CSV
```

Hard rules:

- Doubles stay off the audio task. Detector is float32. Timestamps are `int64` µs. Tempo is milli-BPM `uint32`.
- `ableton::Link` / `ILinkSession` is core-0 only, same as tap / CLK IN / MIDI.
- The pulse GPTimer ISR is not in this path. Detector cost is inside the existing 5.3 ms audio block.

```mermaid
sequenceDiagram
  participant DAC as I2S DMA
  participant AT as audio_task (core 1)
  participant Q as onset_queue
  participant LS as link_service (core 0)
  participant Arb as arbitrate_clock_source
  participant Link as ILinkSession

  AT->>AT: ClickSynth.render (sets last_onsets)
  alt enabled and metro and follow and last_onsets > 0
    AT->>AT: note_click(t0_adc + frame * dt)
  end
  DAC-->>AT: read_block (256 frames)
  AT->>AT: SampleClock.us_at_frame(first) - kAdcLatencyUs
  AT->>AT: OnsetDetector.process (envelope, flux, thresh, guard)
  alt onset and not click-guarded
    AT->>Q: neon::OnsetEvent {t_us, strength}
  end
  AT->>DAC: write_block (unchanged mix)
  LS->>Q: pop all
  LS->>LS: AudioTempoFollower.on_onset
  LS->>Arb: kAuto + liveness
  alt audio_allowed and tempo update
    LS->>Link: set_tempo(mbpm/1000)
  end
```

`audio_service.cpp` already renders click (~397) **before** `read_block` (~441). `note_click` must run after `render` so `last_onsets()` is this block’s count. The ASCII architecture sketch above is the same order.

#### 1. Detector algorithm

New portable class `neon::OnsetDetector` in:

- `components/neon_core/include/neon/audio/onset_detector.hpp`
- `components/neon_core/src/audio/onset_detector.cpp`

Time-domain envelope / differentiator / adaptive threshold. No FFT. No autocorrelation. Per-sample work is a handful of float32 ops — see CPU budget.

**Stereo collapse.** Peak, not mean: a kick on one channel must not be halved.

```
x[i] = max(|L[i]|, |R[i]|)
```

**Envelope (peak hold + 10 ms decay) and slow floor (200 ms).** Time constants are in milliseconds; the detector converts them at `reset(sample_rate)` the same way `ClickSynth::reset` does. Teensy is 44.1 kHz / 128 (`teensy41/src/audio_t41.cpp`); Daisy and ESP are 48 kHz. Do not bake 48000 into the decay literals.

```
kEnvTauMs      = 10.f
kSlowTauMs     = 200.f
kFluxArm       = 1.8f     // onset when flux > thresh * 1.8
kThreshFloor   = 0.008f   // abs floor so silence does not explode
kRefractoryUs  = 40000    // 40 ms: 16ths at 187 BPM still pass; bounce dies
kClickGuardUs  = 8000     // ±8 ms around our own click
kClickGuardCap = 4        // this block + previous clicks still inside ±8 ms
```

At `reset(rate)`:

```
env_decay_  = expf(-1.f / (kEnvTauMs  * 0.001f * rate))  // 48k → ~0.99792
slow_alpha_ = 1.f - expf(-1.f / (kSlowTauMs * 0.001f * rate))
```

`expf` is allowed here: `reset` is not per-sample. Host tests pin the 48 kHz and 44.1 kHz values.

**Sensitivity mapping.** `thresh = slow_ * scale`. **Lower scale = easier to trigger.** The slider is “higher = easier,” so the scale is inverted:

| `sensitivity_` | `scale` | UX |
|---|---|---|
| 0 | 2.00 | pickiest — weak transients rejected |
| 128 (default) | 1.172 | studio kick at ~−6 dB, −20 dB floor |
| 255 | 0.35 | easiest — ghost notes and bleed get through |

```
scale = 2.0f - (sensitivity_ * 1.65f / 255.0f);   // 0→2.00, 128→1.172, 255→0.35
```

Host-test fixture that actually discriminates (slow/floor held at 0.02, attack flux ≈ peak − floor):

| | peak | floor | flux | thresh × 1.8 | result |
|---|---|---|---|---|---|
| sensitivity 255 (`scale = 0.35`) | 0.05 | 0.02 | ≈ 0.03 | 0.02 × 0.35 × 1.8 = 0.0126 | **detect** (`0.03 > 0.0126`) |
| sensitivity 0 (`scale = 2.00`) | 0.05 | 0.02 | ≈ 0.03 | 0.02 × 2.00 × 1.8 = 0.072 | **reject** (`0.03 < 0.072`) |

A 0.15-peak kick on a 0.02 floor (flux ≈ 0.13) clears **both** thresholds (`0.13 > 0.072`) and would not catch a polarity reversal. Pin slow/flux in the test comment so the fixture cannot drift from the formula.

**Pseudocode** (one block). `t0_us` is the SampleClock time of input frame 0 of this block, already in the **ADC** µs domain (see timestamping). `us_per_frame_q32` comes from `SampleClock::us_per_frame_q32()` — **not** `1e6/rate`, and **not** a block index.

```
struct OnsetEvent { int64_t t_us; float strength; };

uint32_t OnsetDetector::process(const float* L, const float* R, uint32_t n,
                                int64_t t0_us, uint64_t us_per_frame_q32,
                                OnsetEvent* out, uint32_t cap) {
  expire_click_guards(t0_us);  // drop stamps older than t0_us - kClickGuardUs
  uint32_t k = 0;
  const float scale = 2.0f - (sensitivity_ * (1.65f / 255.0f));
  for (uint32_t i = 0; i < n; ++i) {
    const float x = fabsf(L[i]) > fabsf(R[i]) ? fabsf(L[i]) : fabsf(R[i]);
    env_ = (x > env_) ? x : env_ * env_decay_;
    slow_ += (env_ - slow_) * slow_alpha_;
    const float flux = env_ - prev_;
    prev_ = env_;

    const float thresh = (slow_ * scale > kThreshFloor) ? slow_ * scale
                                                        : kThreshFloor;
    if (flux <= thresh * kFluxArm) continue;

    const int64_t t_us = t0_us + static_cast<int64_t>(
        (static_cast<uint64_t>(i) * us_per_frame_q32) >> 32);
    if (t_us - last_onset_us_ < kRefractoryUs) continue;
    if (near_click_guard(t_us)) continue;  // any ring entry within ±8 ms

    last_onset_us_ = t_us;
    if (k < cap) out[k++] = {t_us, flux};
  }
  return k;
}
```

**Timestamping.** `read_block()` is non-blocking and runs just before `write_block()`. Input is one DMA period plus ADC group delay behind the output map `SampleClock` already maintains from TX `on_sent` marks.

```
t0_us = clock.us_at_frame(first_frame) - kAdcLatencyUs
```

`kAdcLatencyUs` starts as one block at 48 kHz / 256 = **5333 µs**, as a named constant next to `kDacLatencyUs` in `audio_service.cpp`. Period measurement is delay-invariant, so an uncalibrated constant does not bias BPM. Phase-lock (off by default) *would* need a bench calibration; until that number is measured, `follow_phase` is documented as “best-effort, ±½ block.”

Never stamp “block index × 5.33 ms.” Never stamp `esp_timer_get_time()` at process time — that is the render deadline, not the sample.

**Click-guard (feedback lock).** `ClickSynth::render()` zeros `last_onsets_` every block but does **not** clear `last_onset_frame_` (`click.cpp`). Using `last_onset_frame()` unconditionally plants a fake guard at that in-block index on the ~22 silent blocks between clicks at 120 BPM. Guard **iff** `last_onsets() > 0`.

Clicks and onsets must share one time domain. Onsets are stamped in the ADC map (`t0_us = us_at_frame(first) - kAdcLatencyUs`). Convert the in-block click index with **that same** `t0_us` so `kAdcLatencyUs` is folded into the guard, not subtracted on one side only:

```
const bool click_can_leak =
    cfg.enabled != 0 && cfg.metro_enabled != 0 && cfg.follow_enabled != 0 &&
    source_needed(cfg, neon::AudioRole::kMetronome, /*in_mix=*/true);
if (click_can_leak && g_click.last_onsets() > 0) {
  const int64_t click_us = t0_us + static_cast<int64_t>(
      (static_cast<uint64_t>(g_click.last_onset_frame()) * us_per_frame_q32) >> 32);
  detector.note_click(click_us);          // push into a 4-deep ring
}
```

ESP still **renders** the click when `metro_enabled` even if `audio.enabled == 0` (outputs are cleared afterwards). Guarding on metro+follow alone would drop the drummer’s on-beat kicks — the only hits a kick-mic produces — on the live-kit / IEM path this feature is for. **Do not call `note_click` unless the mix that reaches a jack/speaker actually contains metro** (`cfg.enabled` and the metronome role is in the mix).

`note_click` keeps stamps until they age out of `t_block_end + kClickGuardUs`. Block period is 5.33 ms, guard is ±8 ms, so a click on the last sample of block N still suppresses bleed on the first samples of N+1. A single sticky `next_click_us_` cannot do that.

Host tests:

- No click this block (`last_onsets()==0`) → a kick at `t0 + stale_frame * dt` is **not** dropped.
- Click on last sample of block N, kick 2 ms into N+1 → dropped.
- Click and kick 20 ms apart → kick kept.
- `enabled == 0`: detector is **not** given a click stamp even if `metro_enabled` (on-beat kick at session BPM is kept).

A time gate **cannot** keep an on-grid kick and drop speaker bleed: they are coincident. The 118-vs-120 mixed-train test only works because the tempos walk past each other. Copy must say that, not “use IEMs and leave METRO on speakers”:

> Speaker click + close mic cannot time-separate an on-grid kit. Mute the PA (`AUDIO` off) or turn METRO off. IEMs are fine because the click never enters the ADC.

We do **not** hard-mutex metro and follow — IEM / `enabled == 0` users want both.

**Boards without DIN.** `OnsetDetector` is not called. The AUDIO > FOLLOW row is hidden (honest-menu) or shows `NO ADC` and refuses to latch on.

#### 2. How onsets become tempo

**Do not feed `TapTempo`.** It averages raw intervals, has no subdivision, no 0.5×/2× gate, and a double-kick poisons the run. It is the right *product* analogue (tempo only) and the wrong *estimator*.

**Do not use `MidiClockPll`.** MIDI ticks are authoritative 24 PPQN with Start = beat 0. Drum hits are not a tick stream. A PLL would phase-lock the grid to noisy onsets — the opposite of “like tap-tempo.”

**Do wrap `ExtClockEstimator`.** It already has outlier rejection, median-of-5, EMA, 0.5 % hysteresis, settle pulses, and a 2 s activity timeout. It is the CLK IN estimator, and audio-follow is “CLK IN, but the pulses are detected.” What it lacks is a PPQN that matches “this drummer is playing 8ths.”

New portable class `neon::AudioTempoFollower`:

```
// components/neon_core/include/neon/audio/tempo_follower.hpp

class AudioTempoFollower {
 public:
  enum class Lock : uint8_t { kIdle = 0, kAcquiring = 1, kLocked = 2 };

  void set_session_tempo(uint32_t milli_bpm);  // lock-range prior
  void reset();

  void on_onset(int64_t t_us, float strength);

  bool active(int64_t now_us) const;           // ExtClock-style timeout
  Lock lock_state() const;

  // One-shot, hysteretic, integer-BPM, rate-limited. 0 until locked.
  bool take_tempo_update(uint32_t* milli_bpm);

  uint32_t tempo_milli_bpm() const;            // last published, 0 if none
  uint8_t subdivision() const;                 // 1, 2, or 4
  uint32_t onset_count() const;
  uint32_t rejected_count() const;             // octave / lock-range / bounce
};
```

Internally:

```
AudioTempoFollower
  ├─ SubdivisionPicker   // new, session-tempo prior
  └─ ExtClockEstimator   // existing; ppqn := subdivision
```

`on_onset`:

1. Classify the inter-onset interval against the session beat period.
2. Vote subdivision (see next section); `est_.set_input_ppqn(subdiv)` on change (this already `reset_history()`s inside `ExtClockEstimator` — acceptable; a subdiv change is a relock).
3. `est_.on_pulse(t_us)` only if the IOI classified. Unclassifiable IOIs (ghosts far off the grid, rolls) are counted in `rejected_count_` and dropped.
4. Publish path is **not** `ExtClockEstimator::take_tempo_update` raw. Wrap it with SyncFollower-style policy (below).

**Why wrap publish rather than take ExtClock’s 0.5 % as-is.** 0.5 % at 120 BPM is 0.6 BPM — fine for CLK IN, but audio onsets are noisier, and Link `setTempo` is a proposal the whole session hears. Copy `midi::SyncFollower` numbers, which were chosen exactly to stop tenths-chatter at Live:

| Constant | Value | Source |
|---|---|---|
| Integer BPM | publish `mbpm - (mbpm % 1000)` | `SyncFollower` |
| Schmitt | 0.6 BPM (`kIntegerGuardMbp = 600`) | `sync_follower.hpp` |
| Min gap | 1 s (`kTempoGapUs`) | same |
| Extra slew | clamp Δ to ±2 BPM per publish | audio-specific; “a few BPM over a song” |
| First lock | publish immediately once `kMinIoIs = 4` classified IOIs and ExtClock has an estimate | acquisition |
| Range | `clamp_milli_bpm` (20–999 BPM) | `transport.hpp` |
| Acquiring window | candidate must sit in **session ± 12 %** | a unit stored at 120 can grab a band at 108 (555 ms still classifies as T: `1.12T = 560 ms`) |
| Locked window | candidate must sit in **session ± max(6 BPM, 5 %)** | after `kLocked`; a 20 BPM jump never publishes |

`lock_state()` is `kAcquiring` from the first classified IOI until `kMinIoIs` and ExtClock both have an estimate, then `kLocked`. Silence: `ExtClockEstimator::active()` already clears history after `max(2 s, 4× period)`. Follower drops to `kIdle`, stops proposing, and the session stays at the last published tempo (tap-tempo does the same when you stop tapping). A drummer who actually halves the feel sits outside the locked window until they (or a peer) move the session near the new tempo — same recovery as a bad tap run.

**Phase.** Default off. The follower does **not** implement `take_phase_request`: it has no beat position (`set_session_tempo` is a scalar). `link_service` already has `TimelineSnapshot` and `beat_at_q32`.

Do **not** add a sticky `last_onset() const` (or even a one-shot `take_last_onset` unless a caller needs it). The 10 ms poll would call `request_beat_at_time` every tick while the last onset still looks near a downbeat. Run the proximity test only on onsets **dequeued this poll** — the `while (onset_queue_pop)` loop already has them, no extra follower state, and a fill that is not a downbeat cannot re-anchor from a stale stamp. Sketch, only if the flag is on **and** `kAdcLatencyUs` is benched:

```
// inside the pop loop, after on_onset:
if (cfg.audio_follow_phase && audio_ok &&
    audio_follow.lock_state() == AudioTempoFollower::Lock::kLocked) {
  const int64_t beat_q32 = neon::beat_at_q32(tl, oe.t_us);
  const uint32_t frac = static_cast<uint32_t>(beat_q32 & 0xffffffffu);
  if ((frac < 515396075u || frac > 3779571220u) && oe.strength > kPhaseMin) {
    session.request_beat_at_time(oe.t_us);  // RST IN contract
  }
}
```

v1 still ships the setting (off). Until `kAdcLatencyUs` is benched, the apply block is `if (cfg.audio_follow_phase) { /* reserved */ }`. Recommended default: **leave the flag off.**

#### 3. Subdivision (8ths, 16ths, ghosts)

Drummers do not only play quarters. Close-mic kick on 1 and 3 is `2T`; hats are 8ths/16ths; ghosts must not vote. The session tempo is the prior — the owner already said typical drift is a few BPM, not a cold start from silence.

Beat period, integer, same numerator ExtClock uses. `session_mbpm == 0` falls back to a 120 BPM quarter (500000 µs). **`60e9` is a double literal in C++ — do not write that.**

```
const uint64_t T_us = session_mbpm != 0 ? (60000000000ull / session_mbpm)
                                        : 500000ull;
```

For IOI `dt`:

| Match window | Classification | How it is fed to ExtClock |
|---|---|---|
| `dt ∈ 0.88T .. 1.12T` | quarter | `on_pulse(t)`, `ppqn = 1` |
| `dt ∈ 0.88T/2 .. 1.12T/2` | eighth | `on_pulse(t)`, `ppqn = 2` |
| `dt ∈ 0.88T/4 .. 1.12T/4` | sixteenth | `on_pulse(t)`, `ppqn = 4` |
| `dt ∈ 0.88·2T .. 1.12·2T` | half (kick on 1+3) | **do not feed 2T raw. Do not insert at session `T`.** Split the **measured** IOI: `on_pulse(t - dt/2)` then `on_pulse(t)`, `ppqn = 1` |
| `dt ∈ 0.88·4T .. 1.12·4T` | whole (kick on 1) | same, equally spaced from `dt`: `t - 3dt/4`, `t - dt/2`, `t - dt/4`, `t`, `ppqn = 1` |
| else | reject (ghost, roll, dropout) | — |

Windows are disjoint: `1.12T < 1.76T` (2T low) and `1.12·T/2 = 0.56T < 0.88T`. Triplets are rejected in v1; a 12/8 feel will not lock.

A close-mic kick on beats 1 and 3 is the common live-kit plant (hats below the adaptive floor). Feeding 2T at `ppqn = 1` would publish 60 BPM at a 120 session and the lock window would drop it. Midpoint insertion is what makes that plant lock.

**Midpoints come from `dt`, not session `T`.** ExtClock’s plant is the period stream (median-of-5, 0.5×–2×). Inserting `t - T_us` when the drummer is at 108 BPM (`dt = 1111 ms`, session `T = 500 ms`) feeds periods 611 then 500: the median sticks at 500 (no pull) or ~98 BPM (outside acquiring ±12 % of 120). That is exactly the “stored 120, band at 108” case the acquiring window exists for, and it would fail on the kick-only plant. Integer split:

```
const int64_t dt = t_us - last_onset_us_;          // measured IOI
// 2T, skip synthetics that are not strictly after last_fed_us:
est_.on_pulse(t_us - dt / 2);
est_.on_pulse(t_us);
// 4T:
est_.on_pulse(t_us - (3 * dt) / 4);
est_.on_pulse(t_us - dt / 2);
est_.on_pulse(t_us - dt / 4);
est_.on_pulse(t_us);
```

Classification still uses session `T` (the prior that rejects 8ths-as-quarters). Only the **synthetic pulse times** use `dt/N`. Host-test kick 1+3 at 108 with session 120 (must publish ~108, not 60/98/120) and 1+3 at 123 (must walk toward 123 under the ±2 BPM slew).

**Vote rule (one rule, not two).** Switch `ppqn` only after **3 consecutive classified IOIs of the new class.** No separate “majority of last 8.” `set_input_ppqn` resets ExtClock history; three in a row is the hysteresis that makes that acceptable.

**Default hypothesis.** None. Do not start at `ppqn = 2`. The first classified IOI sets the class (a 500 ms IOI at session 120 is quarters immediately; a 250 ms IOI is eighths; a 1000 ms IOI is halves-with-midpoint). Starting at eighths would feed a kick-only 500 ms IOI at `ppqn = 2`, estimate 60 BPM, miss the lock window, and sit in `kAcquiring` until the voter moved — the owner use-case would fail to lock.

**Octave errors.** Two independent guards:

1. Classification uses session T, so 8ths are labelled 8ths rather than “240 BPM quarters.”
2. After mapping through PPQN, published BPM must sit in the lock window around session tempo (acquiring ±12 %, locked ±max(6 BPM, 5 %)). A 2× estimate is dropped.

**Ghost notes.** Adaptive threshold + 40 ms refractory. Ghosts below the slow floor never become onsets. A buzz roll that does trigger is a burst of IOIs that fail classification and get rejected; ExtClock’s 0.5× gate additionally drops bounce.

**Acquisition without a meaningful session tempo.** Use `Config::tempo_milli_bpm` (the stored/local value, default 120000). Document: set approximate BPM (tap or encoder) *then* enable follow. Cold-start “what BPM is this room” is a beat tracker, which we are not building.

#### 4. Clock-source arbitration

**Do not add `ClockSource::kAudioMaster` in v1.** Reasons:

- The safety switch the owner asked for is “enable in settings,” not a new master mode.
- OLED `SOURCE` still cycles `wrap_int(s, 3)` = AUTO / LINK / EXT and does not even surface `kMidiMaster` (`menu_model.cpp`). Adding a fifth value without first teaching the panel MIDI is the wrong order.
- Under `kAuto`, audio is the lowest-priority live source, which is correct: a jack and a MIDI 24 PPQN stream are cleaner than a mic.

**Do add `audio_follow_enabled` (default 0).** Arbitration grows one permission flag. Audio must **not** key off `midi_allowed`: under `kAuto` with no CLK IN, `midi_allowed` is true so the PLL can stay warm (`SyncFollower` tracks silently even with no ticks). Liveness is `midi_act.following` in `link_service`, the same split CLK IN already uses.

```
struct ClockArbitration {
  bool follow_clk_in = false;
  bool midi_allowed = false;
  bool audio_allowed = false;  // permission only; liveness is midi_act.following
};

constexpr ClockArbitration arbitrate_clock_source(
    ClockSource source, bool clk_in_active, bool audio_follow_enabled) {
  ClockArbitration a;
  a.follow_clk_in = (source == ClockSource::kAuto ||
                     source == ClockSource::kExternalMaster) &&
                    clk_in_active;
  a.midi_allowed = (source == ClockSource::kAuto ||
                    source == ClockSource::kMidiMaster) &&
                   !a.follow_clk_in;
  a.audio_allowed = audio_follow_enabled &&
                    source == ClockSource::kAuto &&
                    !a.follow_clk_in;
  return a;
}

// link_service, after midi_follow.poll():
const bool audio_ok = arb.audio_allowed && !midi_act.following;
```

PR1 host tests assert `audio_allowed` under `kAuto` + flag + no CLK IN **even though MIDI is allowed**. A PR3 (ESP apply) test — or a comment at the `audio_ok` line — asserts `!midi_act.following` is the second conjunct. Do not put `midi_allowed == false` in the arbiter.

Precedence under `kAuto`:

```
CLK IN (jack live)  >  MIDI clock (follower.following())  >  audio follow  >  session
```

| `clock_source` | Audio follow |
|---|---|
| `kAuto` + `audio_follow_enabled` | yes, if CLK IN silent and MIDI not following |
| `kLinkMaster` | **never** (explicit) |
| `kExternalMaster` | never (jack is the pin) |
| `kMidiMaster` | never |

While not allowed, the detector and follower **keep running** (warm estimate, like the MIDI PLL). `take_tempo_update` is consumed and discarded so a stale one-shot cannot fire on handover — same pattern as `ext_clock.take_tempo_update(&scratch_t)` in `link_service.cpp` today.

`kAudioMaster` is listed under Open Questions as a v2 pin if a band runs MIDI clock *and* wants the kit to win. Not needed for the stated use case.

#### 5. Config, JSON, menu, web, status

**Do not append inside `AudioConfig`.** It sits in the middle of `Config` (v4). Growing it would shift v5–v13 fields and corrupt every stored blob. Append at the tail of `Config`, bump `kConfigVersion` 13 → 14, and special-case decode like every prior version:

```
// model.hpp, after mono_theme (v13)

// Appended in v14. Audio-in tempo follow. Off by default — a v13 blob's
// tail padding must not arm a live session.
uint8_t audio_follow_enabled = 0;
uint8_t audio_follow_phase = 0;         // request_beat_at_time; also off
uint8_t audio_follow_sensitivity = 128; // 0..255
uint8_t audio_follow_input = 0;         // 0 = line, 1 = mic (codec)
```

`config_decode`:

```
if (h.version < 14) {
  out->audio_follow_enabled = 0;
  out->audio_follow_phase = 0;
  out->audio_follow_sensitivity = 128;
  out->audio_follow_input = 0;
}
```

`config_sanitize`: bool-clamp the two flags; sensitivity is already a byte; `audio_follow_input` ∈ {0,1}.

JSON lives under the existing `"audio"` object so the editor does not grow a new top-level key (same trick as nested `AudioConfig` vs flat §7 in AUDIOLINK.md):

```
"audio": {
  ...existing keys...,
  "follow_enabled": false,
  "follow_phase": false,
  "follow_sensitivity": 128,
  "follow_input": "line"          // "line" | "mic"
}
```

`config_json.cpp` get/set those four; unknown keys stay ignored (partial-merge already).

**`AudioEngineConfig`** (seqlock to the audio task) gains only what core 1 needs:

```
uint8_t follow_enabled = 0;
uint8_t follow_sensitivity = 128;
uint8_t follow_input = 0;
```

`audio_engine_config()` copies them. **`i2s_needed` does not grow in the config PR.** Turning FOLLOW on after settings land but before the RX consumer exists would start I2S (and G6-hold NVS) for silence. The `follow_enabled` term is added in the ESP apply PR, at the same time `enable_input` becomes runtime.

Follow with `audio.enabled = 0` is a supported mode **once that apply PR lands**: I2S runs because `i2s_needed` includes follow, mix is cleared (ESP already does `if (cfg.enabled == 0) clear outputs` and still `read_block()`s), detector still sees input. Daisy and Teensy today **return** on `enabled == 0` before touching `in` — those ports must run the detector *before* that early-out (see §6).

**OLED `MenuModel`.** `kAudioItems` 9 → 12:

```
AUDIO, METRO, CLICK, SOUND, OUT L, OUT R, LINE IN, PUBLISH, SUB,
FOLLOW, F SENS, F PHASE
```

`FOLLOW` toggles `audio_follow_enabled`. `F SENS` steps sensitivity by 5. `F PHASE` toggles phase re-anchor. Input source is web-only (mic vs line is a codec detail the encoder does not need). Host tests in `test_ui.cpp` cover the new rows.

`MenuModel::kAudioItems` is not the only copy of the labels. Same PR **must** also:

- `main/lcd_service.cpp` — the AUDIO branch hardcodes `i < 9` and its own `{"AUDIO", "METRONOME", …, "SUB"}` array (P4 LCD **and** Tab5). Grow it in lockstep or fall through to `g_menu.item_label(i)`.
- Honest-menu visible-row maps in `matouch_service.cpp` / `jc3248_service.cpp` (AUDIO is already hidden on those boards). P4 LCD has `DIN = -1`: add a visible-row map so FOLLOW / F SENS / F PHASE do not appear on a board that cannot follow. Maps do **not** live in `menu_model.cpp`.
- `design/screens.json` regenerated via `host/sim/neon_screens` in **this** PR (`scripts/gen_design.py --check` already fails on item-count drift; AUDIOLINK.md requires the refresh when `kAudioItems` bumps).

**Web** (`web/src/routes/Audio.tsx`, `web/src/api.ts`). New card **“Follow tempo”** on the Out pane, below Metronome:

- Toggle: Follow incoming audio (off)
- Slider: Sensitivity
- Toggle: Re-anchor phase (off, with warning)
- Select: Input (Line / Mic), hidden when `/api/status` `audio.follow_inputs` has length 1
- Note: “Off by default. CLK IN and MIDI clock outrank this. Does not move phase unless re-anchor is on.”

`follow_inputs` is a **board capability**, not an NVS field. Computed in `web_ui.cpp` from codec/Kconfig (PCM3060 / Daisy / Teensy → `["line"]`; ES8311 with mic mux → `["line","mic"]`; no ADC → `[]`). Hide the control when the list is length 0 or 1.

`plugin/Source/ui/Pages.cpp` mirrors the same three controls (VST editor already has the Audio page).

**Live status.** Extend, do not replace, `app_status_set_ext_clock`:

```
any_external = follow_external || midi_act.following || audio_act.following
app_status_set_ext_clock(any_external)
```

Plus a dedicated tag for the chip/OLED so “FOLLOW AUDIO” is not spelled “EXT”:

```
void app_status_set_follow_source(FollowSource); // none / clk / midi / audio
```

OLED / LCD live face: when audio-locked, footer `FOLLOW AUDIO` and the detected integer BPM. When acquiring, `FOLLOW …`. Web `StatusChip` gains `source_audio`. `/api/status` `audio` object gains (read from `FollowStatus`, **not** `AudioStatus`):

```
"follow": {
  "enabled": true,
  "lock": "locked",          // idle | acquiring | locked
  "subdiv": 2,
  "bpm": 118.0,
  "onset_hz": 3.9,
  "published_mbpm": 118000
},
"follow_inputs": ["line"]    // board capability; see above
```

#### 6. Data path and I2S input lifetime

**Queue.** One POD: `neon::OnsetEvent` in `onset_detector.hpp` (or `neon/audio/types.hpp`). `audio_bus.h` **includes that header** and does not declare a second `struct OnsetEvent` — `SynthEvent` is global-scope today, and a second identical layout is an ODR / conversion trap.

```
// app_state/audio_bus.h
#include "neon/audio/onset_detector.hpp"   // neon::OnsetEvent
bool onset_queue_push(const neon::OnsetEvent&);  // producer, non-blocking
bool onset_queue_pop(neon::OnsetEvent*);         // link_service, non-blocking
```

Implementations:

| Target | Ring | Notes |
|---|---|---|
| ESP | `xQueueCreate(16, sizeof(neon::OnsetEvent))` next to `synth_queue` | `xQueueSend(..., 0)`. PR3. |
| Teensy | `IrqRing<neon::OnsetEvent, 16>` + `SeqLock<FollowStatus>` in `app_state_t41.cpp` | **PR3** (symbols only; detector is PR5). Same header as ESP. |
| Daisy | same in `app_state_daisy.cpp` | **PR3** symbols; detector PR5. |

Overflow **drops the new onset**, same as MIDI ticks (`midi_sync_queue` comment: “drop on overflow: ticks self-heal”). 16 is still plenty (> 2 s of quarters at 30 BPM, a full 16th-note bar at 240). Host tests do not need this queue; they call `on_onset` directly.

Do not `xQueueSend` from a Daisy/Teensy audio callback. Detector-in-ISR + FreeRTOS is undefined.

**When is input started?** Today `enable_input` is compile-time:

```
#if CONFIG_NEON_AUDIO_INPUT
  io_cfg.enable_input = true;
#endif
```

and `I2sAudio::start` does `want_input = cfg.enable_input && pins_.din >= 0`. Duplex is off on AMYboard because a TX+RX ring OOM’d the S3 (`docs/AUDIOLINK.md` §12, `Kconfig.projbuild`).

Follow cannot wait on a rebuild. Change:

- `AudioIoConfig.enable_input` becomes **runtime**, true when any input consumer is on: `follow_enabled || linein_monitor_gain || la_publish_linein`.
- Restart I2S only on a **0→1** input-need edge (and on the existing `i2s_needed` 0→1 / 1→0). A 1→0 edge (follow off while the engine still needs TX) leaves RX running until the next full I2S stop — cheaper than glitching an already-playing metronome / Link Audio stream. Do not restart on every audio-config seqlock.
- IDF shares one `dma_desc_num` across the TX+RX channel pair. There is no separate “4 RX descriptors.” On S3: `AudioIoConfig.dma_desc = 4` when `enable_input`, else **8**. On P4: always 8. Log `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` before and after `io.start`. If `i2s_new_channel` fails with RX, retry TX-only (`enable_input = false`) and surface `FollowStatus.lock = idle` plus `follow_no_adc`.
- `CONFIG_NEON_AUDIO_INPUT` becomes “board has a usable ADC pin map” (default y when `NEON_I2S_DIN >= 0`), not “always allocate RX.” The Kconfig help text is updated so we do not re-fight G6.

Host cannot test the duplex heap. **AMYboard bench (PR3 gate):** with WiFi associated, enable FOLLOW, confirm no reboot, `heap_free_internal` stays above 32 kB, and `i2s_new_channel` with `dma_desc = 4` succeeds. If it does not, ship TX-only fallback and leave AMYBOARD follow off until a smaller RX path is proven. P4 jack is the happier first hardware gate.

**Codec mic vs line.** `audio_follow_input`: 0 = line, 1 = mic. Consumed only by the codec wrapper:

- PCM3060 (AMYboard): hardware line; mic enum is ignored, JSON still accepts it and reports `follow_input: "line"`.
- ES8311 (P4 / RLCD): v1 programs the **ADC** path (currently `es8311_start()` only unmutes DAC + PA — no ADC registers). Mic vs line is an ES8311 analog mux / PGA register pair, selected at start and on config change. Exact register values are an implementation note in `es8311.cpp`, taken from Espressif’s `es8311_codec` driver (ADC power-up, MIC PGA, ALC off — ALC would fight the detector).
- ES7210 (RLCD): 4-ch ADC on the same I2C bus. v1 uses ES8311 ADC if it clocks; ES7210 is a follow-up if the speaker-board mic is the one that actually works. Do not block Part B on ES7210.
- Daisy / Teensy: codec line input. Mic enum ignored.

**Boards without ADC.** `follow_enabled` sanitizes back to 0 if `kPinI2sDin < 0` at apply time on ESP (firmware). Portable sanitize cannot see pins; the service layer does this, like other board-capability clamps.

**Daisy / Teensy `enabled == 0`.** ESP already `read_block()`s every loop and only clears the mix when `enabled == 0` — follow-with-speakers-off is a small insert there. Daisy `audio_callback` does `(void)in` and **returns** after zeroing outputs when `cfg.enabled == 0` (`daisy/src/audio_daisy.cpp`). Teensy `NeonAudioSource` is `AudioStream(0, nullptr)` and likewise bails when disabled (`teensy41/src/audio_t41.cpp`). Those ports:

1. Run the detector **before** the `enabled == 0` early-out (Daisy: stop ignoring `in`; Teensy: actually attach `AudioInputI2S` as 2 inputs, not a 0-input `AudioStream`).
2. Push onsets through `IrqRing`, not FreeRTOS.
3. Still transmit silence when `enabled == 0`.

This is a follow-up port PR after the ESP path ships. The ESP path is the ship gate.

#### 7. `link_service` apply

In the existing 10 ms poll, after MIDI follower `poll()`, before net preference:

```
neon::OnsetEvent oe;
while (onset_queue_pop(&oe)) {
  audio_follow.set_session_tempo(tl-derived mbpm or cfg.tempo_milli_bpm);
  audio_follow.on_onset(oe.t_us, oe.strength);
  // Phase, if ever enabled: proximity test on **this** dequeued onset
  // only (§2). v1: reserved no-op until kAdcLatencyUs is benched.
}
const bool audio_ok = arb.audio_allowed && !midi_act.following;
uint32_t mbpm = 0;
if (audio_ok && audio_follow.take_tempo_update(&mbpm)) {
  session.set_tempo(static_cast<double>(mbpm) / 1000.0);  // the one allowed double, same as tap/CLK/MIDI
  ESP_LOGI(kTag, "audio tempo -> %u BPM", mbpm / 1000);
}
if (!audio_ok) {
  uint32_t scratch = 0;
  audio_follow.take_tempo_update(&scratch);  // consume
}
```

`FollowStatus` is published from this same poll (one writer: link_service).

Teensy (`link_service_t41.cpp`) and Daisy (`link_service_daisy.cpp`) get the same block in the port PR; they already own an `ExtClockEstimator`.

Tap-tempo commands still win as user input: a `kTapTempo` / `kNudgeTempo` is a local edit and does not reset the follower, but the next published audio estimate must pass the lock window around the *new* session tempo. That is automatic because `set_session_tempo` is refreshed every poll from the snapshot.

#### 8. Host tests

New files, added to `host/CMakeLists.txt` and `components/neon_core/CMakeLists.txt`:

**`host/tests/test_onset_detector.cpp`**

- Synthetic 48 kHz kick: 2-cycle 60 Hz sine * exp decay, peak 0.9, at known frame indices. Assert onset `t_us` within 1 sample of the true attack.
- Stereo: kick only on R → still detected (peak collapse).
- Refractory: two hits 10 ms apart → one onset.
- Adaptive thresh: −20 dB noise floor, kicks at −6 dB → detections; noise only → none.
- Click-guard: `note_click` at frame F drops a kick within ±8 ms; a kick 20 ms later is kept. `last_onsets()==0` (stale `last_onset_frame`) does **not** guard. Click on last sample of block N still guards 2 ms into N+1.
- Sensitivity polarity: peak 0.05 on a 0.02 floor (flux ≈ 0.03). Detected at 255 (`thresh*1.8 = 0.0126`), rejected at 0 (`thresh*1.8 = 0.072`). Comment the slow/flux numbers in the test.
- `reset(44100)` vs `reset(48000)`: 10 ms envelope is within 1 ms either way (not a 48 kHz-only object).
- Block-phase: kick at last sample of block N and first of N+1, timestamps monotonic in SampleClock units.
- CPU smoke: 1000 blocks, no NaN, envelope bounded.

**`host/tests/test_audio_tempo_follower.cpp`**

| Case | Input | Expect |
|---|---|---|
| Steady 120 BPM quarters | IOI 500 ms, session 120 | lock, publish 120000, subdiv 1 |
| Steady 120 BPM 8ths | IOI 250 ms, session 120 | lock, publish 120000, subdiv 2 — **not** 240 |
| 16ths at 100 BPM | IOI 150 ms | subdiv 4, 100 BPM |
| Kick on 1+3 only | IOI 1000 ms, session 120 | lock, publish 120000, subdiv 1 (`dt/2` midpoints). **Not** 60. |
| Kick 1+3 at 108, session 120 | IOI 1111 ms | publish ~108 (acquiring ±12 %). **Not** 60 / 98 / 120. Midpoints at `dt/2`, not session `T`. |
| Kick 1+3 at 123, session 120 | IOI 976 ms | walks toward 123 under ±2 BPM slew |
| Snare on 2+4 only | IOI 1000 ms, session 120, first hit at T | same as 1+3 at 120 |
| Hats-too (8ths) | IOI 250 ms plus a louder 500 ms kick | subdiv 2 or 1 after 3 consecutive of one class; publish 120 |
| Acquire at 108 from stored 120 | IOI 555 ms, session 120, `kAcquiring` | publishes 108 (inside ±12 %). After lock, ±max(6 BPM, 5 %) applies |
| Drift +3 BPM over 64 beats | linear IOI shrink, session 120 | last publish ∈ 122000..124000, steps ≥ 1 s apart, each Δ ≤ 2 BPM |
| Dropout 1.5 s then resume | | stays locked if < timeout; relocks after 2 s gap |
| Double-hits +10 ms | | rejected by detector refractory / ExtClock 0.5× |
| Octave: 8ths at 120 | session 120, IOI 250 ms | 120 / subdiv 2, **not** 240 |
| Octave: 16ths at 120 | session 120, IOI 125 ms | 120 / subdiv 4, **not** 480 |
| Metronome self-bleed | click train at session BPM mixed at −12 dB with a 118 BPM kick | follow tracks the kick, not 120, when click-guard is on |
| Phase off | | onsets dequeued this poll must not call `request_beat_at_time` |
| `clamp_milli_bpm` | absurd IOIs | no publish outside 20–999 |
| Arbitration | table in `test_ext_clock.cpp` extended | `audio_allowed` under kAuto+flag+no clk_in **even if MIDI is allowed**; false on kLinkMaster even if flag on |

Generator helper (test-local): `kick_train(bpm, subdiv, drift_bpm_per_min, n_beats, dropout_ranges[], extra_hits[])`.

**`host/tests/test_config.cpp` / `test_config_json.cpp` / `test_ui.cpp`**

- v13 blob (payload size of today’s `Config`) decodes with follow flags 0, sensitivity 128.
- v14 round-trip.
- JSON partial-merge does not enable follow when the key is absent.
- Menu: FOLLOW toggles; kAudioItems = 12.
- `i2s_needed` is **still 0** when only `audio_follow_enabled` is set (config PR). The apply PR adds a test that it becomes 1.

**`host/tests/test_ext_clock.cpp`**

- Extend `"clock_source arbitration: the one precedence table"` with the new `audio_follow_enabled` argument. Keep the existing CLK IN > MIDI cases bit-identical.

#### 9. Observability

**Do not put follow telemetry on `AudioStatus`.** `audio_status_bus()` is published as a whole struct by the **audio task** every ~16 blocks (`main/audio_service.cpp`). link_service writes would be overwritten with zeros; audio-task writes would race. `/api/status` in `web_ui.cpp` reads that same bus.

New seqlock, one writer (`link_service`), same shape as `app_status_set_ext_clock`:

```
// neon/audio/types.hpp
struct FollowStatus {
  uint8_t enabled = 0;
  uint8_t lock = 0;               // 0 idle 1 acquiring 2 locked
  uint8_t subdiv = 0;             // 0 none, else 1/2/4
  uint8_t no_adc = 0;             // RX failed / DIN missing
  uint16_t onset_hz_x10 = 0;      // 39 = 3.9 onsets/s
  uint32_t mbpm = 0;              // smoothed estimate
  uint32_t published_mbpm = 0;
  uint32_t onsets = 0;
  uint32_t rejects = 0;
};

// app_state/audio_bus.h
neon::SeqLock<neon::FollowStatus>& follow_status_bus();
```

Onset rate is computed in the follower from `on_onset` times (10 ms poll is plenty). The ESP apply PR publishes this bus; OLED/web/CSV are consumers of it, not a later invention of the fields.

**CSV.** New `AFOL,` prefix, same idiom as `PLL,` (`neon/telemetry/midi_pll_csv.hpp`) so `tools/studio_mode/uart_telemetry_logger.py --prefix AFOL` works:

```
t_us,lock,subdiv,onset_hz_x10,est_mbpm,pub_mbpm,onsets,rejects,following
```

Emitted at 1 Hz from `link_service` while `audio_follow_enabled` (not per-onset — that would flood UART during 16ths). Host test `test_audio_follow_telemetry.cpp` pins the header.

`/api/status` as in §5. ESP_LOG on lock enter/leave and on each publish (already the CLK IN pattern).

#### 10. CPU budget (256-frame block @ 48 kHz, ESP32-S3)

Block period = 256 / 48000 = **5.333 ms**. Audio task already: click + pulses + mixer + optional gist LPF + optional AMY + Link Audio drain.

Detector per sample: `fabs` ×2, max, 2 MAC (env, slow), sub, compare, integer timestamp. ≈ 12 float ops + a few integer.

256 × 12 ≈ 3072 FLOPs. S3 FPU is single-precision, ~1 cycle add/mul. **~15–40 µs** including the click-guard branch, well under 1 % of the block. No extra buffers. No heap. Stack: `OnsetEvent tmp[8]` on the audio task (8 onsets/block is already a roll; cap and count extras as rejects).

If profiling ever shows otherwise (it will not), downsample ×2 (every other sample) — transients at 24 kHz still beat 40 ms refractory. Not the v1 path.

P4 is faster. Daisy callback is 48 frames / 1 ms; same detector, smaller n. Teensy 128 @ 44.1 uses `reset(44100)` so the 10 ms / 200 ms taus match; the per-sample op count is the same.

---

## API / Interface Changes

### New portable types

```cpp
// neon/audio/onset_detector.hpp
struct OnsetEvent {
  int64_t t_us = 0;
  float strength = 0.f;
};

class OnsetDetector {
 public:
  void reset(uint32_t sample_rate);         // derives env/slow decays
  void set_sensitivity(uint8_t s);          // 0..255, higher = easier
  void note_click(int64_t click_us);        // ADC-domain µs; 4-deep ring
  uint32_t process(const float* L, const float* R, uint32_t n,
                   int64_t t0_us, uint64_t us_per_frame_q32,
                   OnsetEvent* out, uint32_t cap);
};
```

`AudioTempoFollower` as in §2. No onset getter — phase, if ever enabled, uses onsets dequeued this poll.

### Arbitration (signature change — host-tested, all three link services)

```cpp
struct ClockArbitration {
  bool follow_clk_in = false;
  bool midi_allowed = false;
  bool audio_allowed = false;  // NEW: permission only; liveness is midi_act.following
};

constexpr ClockArbitration arbitrate_clock_source(
    ClockSource source,
    bool clk_in_active,
    bool audio_follow_enabled = false);
```

Default argument keeps existing call sites compiling during PR1; PR2 removes the default once every service passes the flag.

### `IAudioIo` / `AudioIoConfig`

No new methods. `enable_input` is already on `AudioIoConfig`. Behavior change: audio_service sets it from consumers, not from `#if CONFIG_NEON_AUDIO_INPUT` alone.

### Control queue

No new `ControlCommand::Kind`. Follow is not a button mash; it is a config-driven source. Taps remain `kTapTempo`.

### REST

No new endpoints. `PUT /api/config` partial-merge of the four `audio.follow_*` keys. `GET /api/status` grows `audio.follow` (from `FollowStatus`) and `audio.follow_inputs` (board capability). Existing clients ignore unknown keys.

---

## Data Model Changes

### NVS blob v13 → v14

Append four bytes at the **end of `Config`**. Decode of a v13 payload:

1. Header version 13, `payload_size == sizeof(Config_v13)`.
2. `*out = Config{}` then `memcpy` the old prefix.
3. `if (h.version < 14)` forces follow fields to defaults (off / 128 / line).
4. `config_sanitize`.

This is the same padding trap v3→v4, v6, v11 already special-cased. A v13 blob’s tail alignment must not be interpreted as `audio_follow_enabled = 1`.

JSON keys are nested under `"audio"` even though the C fields are siblings of `AudioConfig`. Document that split in `config_json.cpp` with a one-line comment; do not flatten.

### Live-applied slice

`AudioEngineConfig` grows three bytes (enabled, sensitivity, input). `i2s_needed` **does not** include follow until the ESP apply PR. Seqlock size change is fine — it is not persisted. `FollowStatus` is a new seqlock, not an NVS field.

### Migration of running units

No behavior change until someone turns FOLLOW on. A unit that never opens the Audio page keeps v13-equivalent operation after the firmware bump (decode fills defaults).

---

## Alternatives Considered

### 1. FFT / autocorrelation beat tracking vs time-domain transients

**FFT / flux / novelty + tempo autocorrelation** (the textbook beat tracker). Would estimate BPM without a session prior, handle mixed percussion, and cost an FFT per block (256-point rfft ≈ several hundred µs on S3, plus a several-second analysis buffer). The audio task already shares core 1 with pulse-adjacent work; AUDIOLINK.md gated AMY on “≤50 % of the block.” A beat tracker is the rest of the budget, and it still fails on octave errors without a prior. The owner’s plant is “a drummer, a few BPM of drift, already on a session.” **Rejected for v1.** Time-domain envelope + differentiator + session-locked IOI is the MIPS-correct plant. A windowed-autocorrelation upgrade can sit behind `AudioTempoFollower::on_onset` later without touching Link.

### 2. Feeding onsets into `TapTempo` vs `ExtClockEstimator` vs MIDI PLL

| | TapTempo | ExtClockEstimator | MidiClockPll / SyncFollower |
|---|---|---|---|
| Plant | human button | analog PPQN | 24 PPQN ticks + Start |
| Outliers | timeout + range | 0.5×–2× + median-5 | Huber clamp + LS reseed |
| Subdivision | none | `set_input_ppqn` | fixed 24 |
| Phase | none (correct) | RST only (correct) | always (wrong for drums) |
| Publish | every qualifying tap | 0.5 % + 3 pulses | integer BPM, 1 s, Schmitt |

**Choice:** `AudioTempoFollower` = subdivision picker + **contained `ExtClockEstimator`** + **SyncFollower publish policy**. TapTempo is the UX analogue, not the estimator. MIDI PLL would phase-lock and assume a tick index we do not have.

### 3. Phase-lock vs tempo-only

The owner asked for tap-tempo. Tap does not call `request_beat_at_time`. Phase-locking a session to a flammed kick yanks MIDI gear and Live’s timeline — high blast radius. **Tempo-only is v1.** Optional `audio_follow_phase` is a config bit defaulting 0. The proximity-to-downbeat test lives in `link_service` (`beat_at_q32`) on onsets **dequeued this poll**, not a sticky follower getter. If we have not calibrated `kAdcLatencyUs`, the apply block stays a reserved no-op rather than a half API.

### 4. `ClockSource::kAudioMaster` vs an independent enable flag

A new enum value is the MIDI-shaped answer (`kMidiMaster` exists). It is also a panel, JSON, VST, sanitize, and wrap_int change, and the OLED SOURCE row still does not list MIDI. The owner’s words are “must be enabled in settings to avoid confusion.” **v1: `audio_follow_enabled` under `kAuto`, disabled on `kLinkMaster`.** Pinning audio over a live MIDI clock is a v2 enum if a user hits that conflict. The enable flag is the safety interlock either way; an enum value without the flag would still be wrong.

---

## Security & Privacy Considerations

- Follow is an unauthenticated LAN-local setting, same posture as the rest of `PUT /api/config`. The existing origin gate and `device_token` on OTA/factory-reset are unchanged.
- Mic/line audio is **not** stored, not published, and not sent to Link Audio unless `la_publish_linein` is already on (independent flag, already off by default). The detector consumes samples and discards them. Status exposes onset *rate* and BPM, not audio.
- Enabling follow starts I2S RX. That is a new always-on ADC path when the flag is on; it is off by default so a unit in a venue does not silently listen.
- No new network surface.

Threat: a peer on the session sees tempo proposals. That is already true of tap-tempo and CLK IN. Mitigated by hysteresis and the lock window so a noisy room cannot spray `setTempo`.

---

## Observability

Covered in Part B §9. Summary:

| Channel | What |
|---|---|
| OLED / LCD footer | `FOLLOW AUDIO` / `FOLLOW …` / off |
| Web StatusChip | `source_audio` |
| `/api/status` `audio.follow` | lock, subdiv, bpm, onset_hz, published_mbpm (from `FollowStatus`) |
| `/api/status` `audio.follow_inputs` | board capability `["line"]` / `["line","mic"]` / `[]` |
| UART `AFOL,` CSV | 1 Hz while enabled |
| ESP_LOG | lock transitions, each publish (integer BPM) |
| `FollowStatus` seqlock | one writer: link_service. Not `AudioStatus`. |

Alerting: none in-firmware. A lock that never leaves `acquiring` is visible on glass; that is the operator signal (gain, sensitivity, no hits).

---

## Rollout Plan

- **Feature flags:** `audio_follow_enabled` default 0. `metro_enabled` default 0. No Kconfig kill switch beyond existing `CONFIG_NEON_AUDIO` (a no-audio board cannot follow).
- **Staged:** PR1 lands code with zero firmware behavior change (host tests only). PR2 exposes settings, still off, and **does not** flip `i2s_needed`. PR3 is the ESP live path **and** its observability (`FollowStatus`, `AFOL,`, `/api/status` `audio.follow`). PR4 is RLCD click bring-up, independent (call site + DIN Kconfig). PR5 is Teensy/Daisy. A unit that never toggles FOLLOW is bit-identical in the Link poll except for an empty `onset_queue_pop` loop after PR3.
- **Rollback:** set FOLLOW off, or flash previous firmware. v14 blobs are a prefix of a hypothetical v15; a rollback to v13 firmware **rejects** a v14 blob (`h.version > kConfigVersion`) and loads defaults — same as every prior bump. Operators who enabled follow and then downgrade lose *all* stored config for that unit unless they saved JSON. Call that out in the PR2 notes; it is the existing versioning rule, not new.
- **Pulse path:** PR3 hardware gate = scope CLK1 jitter with follow on and a kick playing into **P4 jack** line-in (AMYboard duplex is a named heap bench, not the first jitter gate). Detector is MAX-4, not the pulse ISR; expected result is “no change.” If jitter regresses, drop follow to a lower-rate path (every 2nd sample) before touching pulse priority.

---

## Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Octave error (8ths as 240 BPM) | **High** | Session-tempo IOI classifier + acquiring/locked windows. Host tests: 250 ms → 120/subdiv 2; 125 ms → 120/subdiv 4. First IOI sets class (not a ppqn=2 default). |
| Kick-only never locks | **High** | 2T/4T classes insert midpoints, `ppqn = 1`. Host test kick on 1+3. |
| Metronome → mic feedback lock | **High** | Guard only when click can leak (`enabled && metro && follow` and metro in the mix). Time gate cannot separate an on-grid kit from speaker bleed — copy says mute PA or turn METRO off. |
| `setTempo` chatter vs Live | **High** | Integer BPM, 0.6 BPM Schmitt, 1 s gap, ±2 BPM slew — the MIDI follower’s policy. |
| S3 duplex OOM on AMYboard | **High** | Runtime RX only when a consumer is on; S3 `dma_desc = 4` when `enable_input` else 8; fallback to TX-only + `no_adc`. AMYboard heap bench in PR3. First jitter gate is P4 jack. |
| Pulse jitter regression | **High** | Detector is cheap and in the existing audio task; hardware gate on PR3. |
| Wrong I2S pin / codec on RLCD or Tab5 | **High** | PR4 changes Kconfig DIN, sdkconfig, **and** `i2s_audio.cpp` call sites. Tab5 is **not guessed**. |
| Phase re-anchor yanks the band | **Med** | Default off. Separate setting. |
| Fighting CLK IN / MIDI | **Med** | Same arbiter; audio lowest under kAuto; kLinkMaster disables. |
| Ghost notes / rolls poison lock | **Med** | Adaptive thresh, 40 ms refractory, unclassified IOIs dropped, ExtClock 0.5× gate. |
| Input latency uncalibrated | **Low** for tempo (invariant), **Med** for phase | Phase off by default; `kAdcLatencyUs` named for later bench. |
| Config v14 padding trap | **Med** | Explicit `version < 14` reset; host test with a real v13 blob size. |

---

## Open Questions

Only product forks. Each has a recommended default so implementation is not blocked.

1. **Triplets / 12-8.** v1 rejects IOIs that are not 1 / ½ / ¼ / 2 / 4 of T. A jazz drummer in 12-8 will not lock. **Recommend: no triplets in v1.** Add ppqn=3 if someone actually tours it.

2. **`ClockSource::kAudioMaster`.** Needed only to outrank a live MIDI clock. **Recommend: not in v1.** Enable flag + kAuto is the drummer story.

3. **Default subdivision hypothesis.** Resolved in §3: **first classified IOI sets the class.** Do not start at ppqn=2 (that is the kick-only failure mode).

4. **Lock window.** Resolved in §2: **±12 % while `kAcquiring`, ±max(6 BPM, 5 %) once `kLocked`.** First publish after `kMinIoIs = 4`.

5. **Tab5 codec/pin map.** Not in this repo. **Recommend: out of Part A/B PRs; a dedicated bring-up once the board is on the bench.** Do not copy CrowPanel or P4-DEV-KIT numbers.

6. **ES7210 vs ES8311 ADC on RLCD.** **Recommend: ES8311 ADC first** (same init path as P4). ES7210 if the mic that works is on that chip.

7. **Should FOLLOW auto-enable `audio.enabled`?** **Recommend: no.** After the apply PR, `i2s_needed` includes follow. Leaving `audio.enabled = 0` keeps speakers silent while following — the live-kit case. Daisy/Teensy must not early-out before the detector.

---

## Key Decisions

1. **Part A is gap-fill.** `ClickSynth`, mixer, menu, web, and the render loop already exist. The RLCD click PR must change **Kconfig DIN=10, sdkconfig `NEON_AUDIO=y`, `es8311.cpp` compile guards, and `i2s_audio.cpp` start/stop call sites**. PA 46 already exists. `metro_enabled` stays 0. P4 LCD speakers already work. Tab5 waits on a measured pin map.

2. **Tempo-only, tap-tempo analogue.** `session.set_tempo` only. Phase, if ever enabled, is decided in `link_service` via `beat_at_q32` on onsets **dequeued this poll**, not a sticky follower getter.

3. **`AudioTempoFollower` = subdivision picker + existing `ExtClockEstimator` + `SyncFollower` publish policy.** Not `TapTempo`. Not `MidiClockPll`. 2T/4T IOIs insert **equally spaced pulses from the measured `dt`** (`t - dt/2`, not session `T`) so a kick-only kit a few percent off the stored tempo still locks. First classified IOI sets the class.

4. **Time-domain onset detector, not an FFT beat tracker.** Envelope + flux + adaptive threshold + 40 ms refractory. `reset(sample_rate)` derives decays. Sensitivity **higher = easier** (`scale = 2.0 - s*1.65/255`). CPU ≈ 1 % of a 5.3 ms block.

5. **Timestamps are SampleClock µs**, `t0 + (i * us_per_frame_q32) >> 32`, ADC domain (`us_at_frame(first) - kAdcLatencyUs`). Click-guard uses the same `t0_us` and `last_onsets() > 0` only.

6. **Feature flag, not a new `ClockSource`.** `audio_follow_enabled` default 0. Under `kAuto`: CLK IN > MIDI (following) > audio > session. `audio_allowed` is permission; `audio_ok` also requires `!midi_act.following`. `kLinkMaster` disables. `kAudioMaster` deferred.

7. **Config fields append to `Config`, not `AudioConfig`.** `AudioConfig` is mid-struct (v4). v13→v14 decode forces the new bytes off so tail padding cannot arm follow.

8. **Core 1 never calls Link.** One `neon::OnsetEvent` in the detector header; `audio_bus.h` includes it. ESP: `xQueue` depth 16, **drop the new onset** on overflow. Teensy/Daisy: `IrqRing<OnsetEvent, 16>`. The shared-header **symbols** land in PR3 so T41/Daisy still link; detector-in-callback is PR5. Detector-in-ISR + FreeRTOS is undefined.

9. **Click-guard ring, not a mutex.** Guard only when the click can leak into the ADC (`enabled && metro && follow` and metro in the mix). `enabled == 0` (silent speakers / IEM) must not drop on-beat kicks. A time gate cannot separate an on-grid kit from speaker bleed — mute the PA or turn METRO off. Ring is ADC-domain, 4-deep, iff `last_onsets() > 0`.

10. **Runtime duplex.** `enable_input` follows consumers. Restart I2S only on a 0→1 input-need edge. S3 `dma_desc = 4` when `enable_input`, else 8; P4 always 8. Failure falls back to TX-only.

11. **Follow can run with `audio.enabled = 0` on ESP** (mix cleared, `read_block` still runs). Daisy/Teensy must run the detector *before* their `enabled == 0` early-out. `i2s_needed` gains the follow term in the apply PR, not the config PR.

12. **Integer BPM, 1 s, ±2 BPM slew, two lock windows.** Acquiring ±12 %, locked ±max(6 BPM, 5 %). Same anti-chatter posture as MIDI follow.

13. **Follow telemetry is `FollowStatus`, one writer (`link_service`).** It is not a field on `AudioStatus`. The live apply PR publishes it so a merged path is observable.

---

## References

- [`docs/AUDIOLINK.md`](docs/AUDIOLINK.md) — click, I2S, SampleClock, mixer, G6 I2S lifetime, duplex heap incident
- [`docs/SPIKE_MIDI_PLL.md`](docs/SPIKE_MIDI_PLL.md) — why ExtClock is not a PLL; SyncFollower publish policy
- [`docs/LINKSYNC_P4LCD.md`](docs/LINKSYNC_P4LCD.md) — NS4168 speakers, metronome already opt-in
- [`docs/LINKSYNC_RLCD.md`](docs/LINKSYNC_RLCD.md) — ES8311/ES7210 present, audio engine currently off
- [`components/neon_core/include/neon/clock_arbitration.hpp`](components/neon_core/include/neon/clock_arbitration.hpp)
- [`components/neon_core/include/neon/ext_clock.hpp`](components/neon_core/include/neon/ext_clock.hpp)
- [`components/neon_core/include/neon/transport.hpp`](components/neon_core/include/neon/transport.hpp) — `TapTempo`, `clamp_milli_bpm`
- [`components/neon_core/include/neon/midi/sync_follower.hpp`](components/neon_core/include/neon/midi/sync_follower.hpp)
- [`main/audio_service.cpp`](main/audio_service.cpp) — render loop, `read_block`, `i2s_needed`
- [`main/link_service.cpp`](main/link_service.cpp) — tap / CLK IN / MIDI apply
- [`llm-wiki/concepts/honest-menu.md`](llm-wiki/concepts/honest-menu.md) — hide settings that drive absent hardware

---

## PR Plan

Incremental, independently reviewable, mergeable. PR1 has no firmware behavior change. The live `setTempo` path (PR3) ships with lock state visible. Teensy/Daisy and RLCD click are separate failure domains.

### PR1 — Portable detector + follower + host tests

- **Title:** `audio: onset detector and tempo follower (host-tested, inert)`
- **Files / components:**
  - `components/neon_core/include/neon/audio/onset_detector.hpp`
  - `components/neon_core/src/audio/onset_detector.cpp`
  - `components/neon_core/include/neon/audio/tempo_follower.hpp`
  - `components/neon_core/src/audio/tempo_follower.cpp`
  - `components/neon_core/CMakeLists.txt` (add the two .cpp)
  - `host/tests/test_onset_detector.cpp`
  - `host/tests/test_audio_tempo_follower.cpp`
  - `host/CMakeLists.txt`
  - `components/neon_core/include/neon/clock_arbitration.hpp` — add `audio_allowed` and the extra argument **with a default of false** so `link_service.cpp` / Teensy / Daisy still compile unchanged
  - `host/tests/test_ext_clock.cpp` — new arbitration rows; existing CLK IN > MIDI cases remain
- **Depends on:** nothing
- **Description:** Implement `OnsetDetector` (`reset(rate)`, inverted sensitivity, click-guard ring) and `AudioTempoFollower` (2T/4T **`dt/N` midpoints**, first-IOI class, acquiring/locked windows, **no** sticky `last_onset()`, **no** `take_phase_request`). No `main/` glue, no config fields, no I2S changes. CI gate: host ASan/UBSan — 8ths≠240, 16ths≠480, kick 1+3 at 120 → 120, kick 1+3 at 108 with session 120 → ~108, sensitivity 255 detects a 0.05/0.02 kick that 0 rejects, stale `last_onset_frame` does not guard, `audio_allowed` under kAuto+flag+no clk_in even though MIDI is allowed, `kLinkMaster` never allows audio.

### PR2 — Config / menu / web (still off; I2S behavior unchanged)

- **Title:** `config v14: audio follow settings, default off`
- **Files / components:**
  - `components/neon_core/include/neon/config/model.hpp` — append four bytes, `kConfigVersion = 14`
  - `components/neon_core/src/config_model.cpp` — sanitize, v13→v14 decode, `audio_engine_config()` copies follow bytes. **`i2s_needed` formula unchanged.**
  - `components/neon_core/include/neon/audio/types.hpp` — `AudioEngineConfig` follow fields and **`FollowStatus`** (the struct; the seqlock is PR3)
  - `components/neon_core/src/config_json.cpp`
  - `components/neon_core/src/menu_model.cpp` + `include/neon/ui/menu_model.hpp` (`kAudioItems` 9→12)
  - `main/lcd_service.cpp` — AUDIO labels / `i < 9`; P4 LCD honest-menu map hiding FOLLOW on `DIN = -1`
  - `main/matouch_service.cpp`, `main/jc3248_service.cpp` — visible-row maps (AUDIO already hidden)
  - `host/tests/test_config.cpp`, `test_config_json.cpp`, `test_ui.cpp`
  - `design/screens.json` via `host/sim/neon_screens` (`gen_design.py --check`)
  - `web/src/api.ts`, `web/src/routes/Audio.tsx`, `scripts/gen_design.py` strings
  - `plugin/Source/ui/Pages.cpp`
  - `components/web_ui/www/dist/index.html.gz` (bundle)
  - Remove the defaulted extra argument from `arbitrate_clock_source`; update `main/link_service.cpp`, `teensy41/src/link_service_t41.cpp`, `daisy/src/link_service_daisy.cpp` to pass `neon_config().audio_follow_enabled` — **still do not call the follower** (no onset queue yet)
- **Depends on:** PR1
- **Description:** Persist and expose FOLLOW / sensitivity / phase / input. Every stored v13 blob decodes with follow off. Enabling FOLLOW does **not** start I2S. Web/OLED copy states off-by-default and CLK IN/MIDI precedence. P4 LCD does not grow FOLLOW rows it cannot use.

### PR3 — ESP onset path + Link apply + observability

- **Title:** `audio follow: I2S onsets drive Link tempo (ESP)`
- **Files / components:**
  - `components/app_state/include/app_state/audio_bus.h` + `src/audio_bus.cpp` — `#include` `neon::OnsetEvent`, ESP `xQueue` (depth 16, drop-new), `follow_status_bus()`
  - `teensy41/src/app_state_t41.cpp` and `daisy/src/app_state_daisy.cpp` — matching `IrqRing<neon::OnsetEvent, 16>` and `SeqLock<FollowStatus>` **in this PR**, even if unused until PR5. Those files already compile the same `audio_bus.h` (`synth_queue_*`). Shipping the header without the symbols breaks T41/Daisy link.
  - `main/audio_service.cpp` — runtime `enable_input`, `dma_desc` 4 vs 8, detector per block, `note_click` only when `enabled && metro && follow` and metro is in the mix; I2S restart only on 0→1 input-need; `i2s_needed` gains the follow term
  - `main/link_service.cpp` — drain queue, `AudioTempoFollower`, `set_tempo` under `audio_ok = arb.audio_allowed && !midi_act.following`, publish `FollowStatus`, 1 Hz `AFOL,` CSV
  - `components/neon_core/include/neon/telemetry/audio_follow_csv.hpp` + `.cpp` + `host/tests/test_audio_follow_telemetry.cpp`
  - `components/web_ui/src/web_ui.cpp` — `/api/status` `audio.follow` from `FollowStatus`, `audio.follow_inputs` from board capability
  - `components/neon_hal_esp/src/i2s_audio.cpp` — already has `enable_input`; log heap on start
  - `components/neon_hal_esp/src/es8311.cpp` — ADC power-up; honor `follow_input` mic vs line (P4DEVKIT in this PR; RLCD compile/call is PR4)
  - `main/Kconfig.projbuild` — `NEON_AUDIO_INPUT` help text: capability, not “always duplex”
  - `host/tests/test_config.cpp` — `i2s_needed` is 1 when only follow is set (this PR, not PR2)
- **Depends on:** PR2
- **Description:** The ESP live path, observable on day one. Follow off ⇒ empty pop loop and no RX. Follow on ⇒ onsets in SampleClock µs, tempo proposals with MIDI-style hysteresis. Phase apply is reserved unless `kAdcLatencyUs` is benched. Hardware gate: **P4 jack** first (kick or click track in, session BPM tracks ± a few BPM, CLK1 jitter unchanged). METRO+speakers+close mic cannot time-separate an on-grid kit — mute PA or turn METRO off. AMYboard is a **heap bench** (`dma_desc = 4`, WiFi up, no reboot), not the first jitter gate. Boards with `DIN = -1` never start RX. Teensy/Daisy **link** after this PR (empty rings); they do not yet run the detector.

### PR4 — RLCD metronome-output gap-fill

- **Title:** `audio: RLCD ES8311 click path (still off by default)`
- **Files / components:**
  - `sdkconfig.defaults.linksync-rlcd` — `CONFIG_NEON_AUDIO=y`, `CONFIG_NEON_I2S_DIN=10`
  - `main/Kconfig.projbuild` — `default 10 if NEON_BOARD_LINKSYNC_RLCD` on `NEON_I2S_DIN` (MCLK/BCLK/LRCLK/DOUT already have RLCD defaults; DIN does not)
  - `components/neon_hal_esp/src/es8311.cpp` — compile the real driver for `P4DEVKIT || LINKSYNC_RLCD`
  - `components/neon_hal_esp/src/i2s_audio.cpp` — `es8311_start()` / `es8311_stop()` call sites, same guard. Extending the stub without this is a no-op.
  - `docs/LINKSYNC_RLCD.md` — “AUDIO > AUDIO ON, then METRONOME ON,” same shape as `docs/LINKSYNC_P4LCD.md` §Speakers
- **Depends on:** none strictly (click does not need follow). Merge-cleanest after PR3 so ES8311 ADC+DAC init is one function.
- **Description:** Make “click out when hardware can” true for RLCD. Confirm I2C 13/14 is already up (`app_main.cpp`). Do not default metro on. Do not enable audio on MaTouch/JC3248/C3/EPD/XIAO. Do not invent Tab5 pins. P4 LCD is already done. `p4devkit` stays audio-off per its own comment.

### PR5 — Teensy / Daisy ports + remaining UI chrome

- **Title:** `audio follow: Teensy and Daisy ports`
- **Files / components:**
  - `teensy41/src/audio_t41.cpp` — attach `AudioInputI2S` (2 inputs); detector **before** `enabled == 0` early-out; `reset(44100)`. (`IrqRing` / `follow_status_bus` already landed in PR3.)
  - `teensy41/src/link_service_t41.cpp` — same apply block as ESP
  - `daisy/src/audio_daisy.cpp` — stop ignoring `in`; detector before `enabled == 0` early-out
  - `daisy/src/link_service_daisy.cpp`
  - `web/src/components/StatusStrip.tsx` / `StatusChip` — `source_audio`
  - OLED/LCD live footer (`lcd_service.cpp`, RLCD/EPD faces as applicable)
  - `docs/AUDIOLINK.md`, `docs/FEATURES.md`, `README.md`
- **Depends on:** PR3
- **Description:** Non-ESP audio callbacks cannot use FreeRTOS queues and today return before input. Wire the detector/apply path through the `IrqRing`s PR3 already added so T41/Daisy stayed green. Remaining chrome (FOLLOW AUDIO footer, StatusChip, docs) does not block the ESP ship gate — lock is already on `/api/status` and UART from PR3.
