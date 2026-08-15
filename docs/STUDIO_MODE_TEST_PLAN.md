# Studio Mode — test plan

**Question:** does running Link Audio over the module's own SoftAP, with the
core-0 priority inversion fixed, make the feature shippable without new
hardware?

**Owner:** JC · **Time box:** 4 working days, hard stop · **Status:** draft

---

## 1. Hypothesis

Link Audio's receive path fails not because the ESP32-S3 lacks capacity, but
because of three contention sources that are individually fixable:

- **H1 (topology).** Infrastructure mode transmits every audio packet over the
  air twice — module → AP → Mac — through a queue we don't control. Direct AP
  mode halves airtime and removes the router's store-and-forward latency.
- **H2 (scheduler).** Link's asio service runs at priority 8, below the audio
  pump at 11. Audio TX preempts the timing protocol that the product exists to
  deliver.
- **H3 (bus).** WiFi/lwIP buffers share the octal PSRAM bus (SPI0) with ~416 KB
  of audio rings and with flash. This one is *not* addressed by either fix
  above, and is the residual we're trying to size.

If H1 and H2 dominate, the feature ships in firmware. If H3 dominates, no
amount of radio work helps and the feature gets cut or moves to wired
transport.

**Falsifiable claim:** in AP mode with priorities corrected, the module
sustains 30 minutes of unfiltered Link Audio at `la_jitter_ms ≤ 60` with zero
dropped blocks, and Link phase error at CLK1 is statistically indistinguishable
from the Link-only baseline.

---

## 2. What we are not measuring

- **Subjective audio quality.** Every run uses `la_fullband = 1`. The gist
  filter is concealment; leaving it on measures the concealment, not the
  transport. If the transport is clean the filter becomes a tone option
  instead of a crutch.
- **Publish (TX) path.** Receive is where all the mechanism lives. Publish gets
  its own pass later.
- **Range.** All runs at 2 m, line of sight, fixed orientation.

---

## 3. Prerequisites — do not start measuring without these

### P1. Fix the I2S configuration (review §C1) — blocking

`data_bit_width` is 16 while the driver is handed hand-packed `int32_t`. Until
this is resolved and verified on a scope, the local metronome is suspect and
every downstream measurement is meaningless. Confirm BCLK/WS/DOUT against the
PCM3060 strapping, and confirm `bit_shift` matches the part's expected format.

**Exit:** local metronome and synth voice clean at full scale, scope-verified.

### P2. Fix the SampleClock frame domain (review §C2) — blocking

`us_at_frame(frames_written())` drifts permanently after any DMA underrun
because `auto_clear` increments `isr_frames_` without a matching write. Phase
error at CLK1 *is* the primary metric, so a bug that silently shifts it
invalidates the whole exercise.

**Exit:** forced underrun (block the audio task 100 ms) shows phase recovery to
baseline within 2 s.

### P3. Raise the jitter target ceiling (review §C5)

`JitterBuffer::configure` clamps target to `capacity_/2` = 341 ms. We're
descending, not ascending, so this doesn't block — but log the effective target
alongside the requested one so no run is recorded against a number that was
silently clamped.

### P4. Telemetry

One CSV line per second over UART (not the web UI — see §6 confounds), fields:

```
uptime_ms, mode, prio_set, req_jitter_ms, eff_jitter_ms,
jit_fill_frames, jit_underruns, jit_conceals, jit_state,
rx_dropped, rx_high_water, la_trim_ppm,
i2s_write_failures, rssi, heap_free_internal, heap_free_psram
```

Counters are cumulative; the analysis script diffs them. Most of these already
exist on the status bus — this is plumbing, not new instrumentation.

### P5. Measurement rig

Preferred (automated, every downbeat captured):

- Live project: one-sample impulse on beat 1 of every bar, routed to a
  dedicated interface output. Fixed tempo, 120 BPM. Same project every run.
- Record two channels into a DAW at 96 kHz (10.4 µs resolution):
  - **Ref:** interface output, looped back to an input.
  - **DUT:** CLK1 through a 10:1 resistive divider into a line input.
- Offline Python: detect edges on both, compute per-downbeat delta.

**Phase error** = the distribution of (DUT − Ref) across the run. Absolute
offset is uninteresting and includes fixed interface and DAC latency; report
**stddev, p99 |deviation from run median|, max excursion, and linear drift
across the run**.

Scope on CLK1 as a live sanity check only. Don't eyeball 30 minutes of it.

---

## 4. Test matrix

### Phase 0 — kill gate (1 hour)

**Does Link discovery multicast traverse the SoftAP to an associated station?**

Force AP mode, join the Mac, open Live. Does the module appear as a Link peer
and hold tempo for 10 minutes?

ESP32 SoftAP multicast forwarding to stations is exactly the kind of thing that
quietly doesn't work. If it fails, stop — the entire Studio Mode idea is dead
and the answer reverts to Link-only v1 with the C5 path unchanged. One hour
spent to avoid four days.

### Phase 1 — baselines (2 runs × 30 min)

Link only, audio subsystem disabled. Establishes the phase-error reference each
audio cell is compared against, and separates "AP mode changes sync" from
"audio changes sync."

| Run | Mode | Audio |
|---|---|---|
| B-STA | Infrastructure | off |
| B-AP | SoftAP | off |

If B-AP is materially worse than B-STA, AP duty alone costs sync and that's a
finding in itself.

### Phase 2 — staircase (4 cells × ~40 min)

Full 2×2. For each cell, find the **minimum viable jitter target**: start at
200 ms, step down 200 → 120 → 80 → 60 → 40 → 25, five minutes per step. Record
the lowest step that completes with zero `rx_dropped` and zero `jit_underruns`.

| Cell | Mode | Priorities |
|---|---|---|
| A | Infrastructure | current (asio 8, pump 11) |
| B | Infrastructure | fixed (asio 12, link_svc 10, pump 9) |
| C | SoftAP | current |
| D | SoftAP | fixed |

The 2×2 is the point. One combined run tells you it works; the factorial tells
you *which factor bought it*, which is what determines whether you ever need
the C5.

### Phase 3 — confirmation soak (1 run × 60 min)

Best cell, at its minimum viable jitter target **+ one step of margin**.
60 minutes, unattended, with the RF environment characterized before and after.

---

## 5. Pass criteria

The feature ships in firmware if the Phase 3 soak meets **all** of:

| Metric | Threshold |
|---|---|
| Effective jitter target | ≤ 60 ms |
| `rx_dropped` over 60 min | 0 |
| `jit_underruns` over 60 min | ≤ 1 |
| `i2s_write_failures` | 0 |
| Phase error p99 vs. matched baseline | within 1 ms |
| Phase error drift across run | < 2 ms |
| `la_fullband` | 1 (filter off) |

The phase-error clause is the one that can't be traded away. A build that
streams audio beautifully and degrades CLK1 is a worse product than the one
that already works.

---

## 6. Confounds to control

- **No web UI during runs.** Every `PUT /api/config` writes NVS, and on the S3
  flash and PSRAM share SPI0 — a config write stalls both cores' cache misses
  and is a guaranteed glitch. Set config before the run, then leave it alone.
  All telemetry goes over UART.
- **Interleave cell order**, and run each Phase 2 cell twice non-adjacently.
  RF environments drift over a day; don't let 4 p.m. become a variable.
- **Characterize RF before and after each run.** `GET /api/scan` (before the
  run starts), logged: AP count, channel histogram, strongest neighbor RSSI. A
  clean result in a quiet environment means something different than a clean
  result at 40 visible SSIDs.
- **Fixed everything else:** same Live project, same buffer size, same
  interface, same distance and orientation, same Mac, nothing else on the AP.
- **Watch thermals.** `WIFI_PS_NONE` plus AP duty in a closed case is a real
  power and heat load; log it if the module is enclosed.

---

## 7. Decision matrix

| Result | Decision |
|---|---|
| Phase 0 fails | Studio Mode dead. Link-only v1. Re-evaluate C5 + external antenna as the only wireless path. |
| Cell B passes (STA + priority fix) | It was a scheduling bug. Ship Link Audio in both modes. No hardware, no mode split. Best outcome. |
| Only D passes | Ship the two-mode split: **Stage** (infrastructure, Link-only) and **Studio** (AP, Link Audio). No hardware. |
| C and D both pass, B doesn't | Topology dominates. Same two-mode ship. Priority fix goes in anyway. |
| None pass, but D's floor is materially better than A's | H1 real but insufficient. Link-only v1; C5 + external antenna now justified; re-run this plan on that hardware. |
| D ≈ A | The radio is not the bottleneck — H3 (SPI0/PSRAM) dominates. No radio hardware fixes this. Cut receive, ship Link-only, revisit as USB Audio Class. |

That last row is a real possible outcome and the plan is built to detect it.
It's the one that saves the most money.

---

## 8. Schedule

| Day | Work |
|---|---|
| 1 | P1, P2 (blocking fixes) + scope verification |
| 2 | P4 telemetry, P5 rig, analysis script; Phase 0 gate |
| 3 | Phase 1 baselines + Phase 2 staircase |
| 4 | Phase 2 repeats, Phase 3 soak, write-up |

**Hard stop at day 4.** If the answer isn't clear by then, the answer is
"Link-only v1" and the decision moves to hardware on a separate track. Four
attempts have already gone into this; the value of this plan is that it
terminates.

---

## 9. Ship consequences if Studio Mode passes

Not scope for this plan, but they follow immediately and shouldn't be
discovered later:

- **`ap_require_pass` must default to 1**, with a MAC-derived password shown on
  the OLED. Promoting SoftAP from fallback to a primary audio path makes the
  open-AP-plus-unauthenticated-OTA chain (review §R1) materially worse, not
  better. This blocks any Studio Mode release.
- **Two-mode UI.** Stage / Studio as a top-level, legible choice, not a hidden
  toggle. Studio Mode is single-machine by construction — a bandmate's laptop
  can't join your session without joining your AP, at which point contention
  returns. Say so in the UI.
- **No auto-negotiation in v1.** CoreWLAN association needs entitlements the
  plugin can't have inside Live's process; it would take a separate signed
  helper app. V1 is "join the module's AP manually."
- **Recommend USB-C Ethernet for internet**, above Wi-Fi in service order — not
  a second Wi-Fi adapter. macOS third-party Wi-Fi support is poor; Ethernet is
  $20 and kext-free, and keeps plugin licensing traffic routing correctly.

---

## Appendix A — where this plan's assumptions stand in the code today

This plan was written against a review (§C1, §C2, §C5, §R1) whose fixes have
since landed on `main` (commit `c3acf18`, "Harden the web API and fix the
audio-path bugs from the security review"). That changes what Phase 2's
"current" column means and what P1-P4 actually require building. Read this
before running anything below.

### §C1 (I2S bit-width) and §C2 (SampleClock frame-domain drift) — fixed

Both are fixed and merged:

- `components/neon_hal_esp/src/i2s_audio.cpp:105-135` — `I2S_DATA_BIT_WIDTH_32BIT`
  / `slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT`, matching the hand-packed
  32-bit slots (was 16-bit, which consumed each packed word as two samples).
- `components/neon_hal_esp/src/i2s_audio.cpp:253-267` (`write_block`) and
  `:40-51` (`on_dma_sent`) — `frames_written_` re-anchors against the ISR
  frame mark after DMA starvation, so `auto_clear` silence no longer
  permanently shifts `SampleClock`'s presentation times.

P1/P2's exit criteria (scope-verified metronome, forced-underrun phase
recovery) are still worth re-confirming on your specific hardware before
trusting Phase 1+ results, but there is no known bug left to fix here. Note:
`components/neon_hal_esp/` is ESP-IDF-only and has zero host-test coverage —
a fake I2S driver to exercise `write_block()`'s starvation-resync branch off
hardware does not exist. If this plan's execution turns up a regression here,
that fake is worth building before re-fixing blind.

### §C5 (JitterBuffer capacity/target clamp) — addressed differently than assumed

The plan's P3 says "raise the ceiling... but log effective alongside
requested." What actually shipped: the ring grew from 32768 to 131072 frames
(`main/audio_service.cpp`'s `kJitterRingFrames`, `components/neon_core/include/
neon/audio/jitter_buffer.hpp`'s `kMaxJitterMs = 800`) so the *existing*
clamp-to-half-capacity in `configure()`
(`components/neon_core/src/audio/jitter_buffer.cpp`) can honor the full 5-800
ms range without truncating. The clamp itself is intentional and stays — the
servo needs somewhere to go — so P3 is done except for the telemetry half,
which this session added: `JitterBuffer::effective_jitter_ms()` reports what
actually got applied, separate from `jitter_ms()`'s post-sanity-clamp
"requested" value. Both are now on `AudioStatus` (`req_jitter_ms` /
`eff_jitter_ms`) and in the P4 CSV stream, so a clamp is visible instead of
silent.

### §R1 (open AP / unauthenticated OTA) — fixed, relevant to §9

`ap_require_pass` already defaults to 1
(`components/neon_core/include/neon/config/model.hpp:135`,
`kDefaultApPass = "link1234"`), and `config_sanitize` fails closed: a
too-short AP password is replaced with the default rather than opening the
network (`components/neon_core/src/config_model.cpp`). §9's ship-consequence
("`ap_require_pass` must default to 1... blocks any Studio Mode release") is
therefore already satisfied — the MAC-derived-password-on-OLED refinement is
still open, but the hard blocker is not. Host coverage:
`host/tests/test_config.cpp` ("the AP requires a password out of the box",
"an AP password shorter than WPA2 allows fails closed"). Not covered on
host: the Host/Origin CSRF hardening in `components/web_ui/src/web_ui.cpp`'s
`check_local_origin` — that file is httpd-coupled and isn't in the host
build.

### Phase 2's priority columns — the "current" state no longer exists at rest

The priority fix is also already shipped: Link's asio service task runs at
12 (`components/ableton_link/link_overrides/ableton/platforms/esp32/
Context.hpp`), the Link Audio pump at 9
(`components/ableton_link/src/link_audio_esp.cpp`), `link_svc` at 10
(`main/link_service.cpp`) — matching the plan's "fixed" column, not the
"current (asio 8, pump 11)" column Phase 2 wants to compare it against. Run
those numbers as a normal boot and there is nothing to A/B.

To keep the 2×2 factorial the plan is built around (cell B/D vs. A/C — "which
factor bought it"), this session added a debug-only runtime override:
`Config::priority_profile` (`neon::PriorityProfile::kFixed` /
`kLegacy`, in `components/neon_core/include/neon/config/model.hpp`),
settable via `PUT /api/config`'s `debug.priority_profile` field
(`"fixed"` | `"legacy"`), applied once at boot in `main/app_main.cpp`
before any task that could touch it starts (`ablink::set_link_asio_priority`
/ `set_link_pump_priority`, `components/ableton_link/include/ablink/
priority.hpp`). `kLegacy` reproduces the exact pre-fix numbers (asio 8,
pump 11) so Phase 2 cells A and C are runnable without a separate firmware
build. `kFixed` is always the default and always what a normal boot uses —
this exists purely to make the regression checkable, not as a shipped mode.
Host-tested in `host/tests/test_config.cpp` ("link task priorities: fixed
keeps asio above the pump, legacy inverts it").

### P4 (telemetry) — did not exist; built this session

Before this session, the counters P4 wants existed in three incompatible
places: a `%d`-formatted `ESP_LOG` line rate-limited to ~5 s (parsed by
nothing), `AudioStatus` under different names (`fill_ms` not
`jit_fill_frames`, `concealed` not `jit_conceals`, `sub_state` not
`jit_state`, `trim_ppm` not `la_trim_ppm`), or not exposed at all
(`rx_high_water`, `i2s_write_failures` as its own counter, heap, RSSI,
requested-vs-effective jitter, which priority profile is active). None of it
was a flat, parseable, one-line-per-second stream.

What this session added, matching the plan's schema exactly:

- `AudioStatus` (`components/neon_core/include/neon/audio/types.hpp`) gained
  `rx_high_water`, `i2s_write_failures`, `heap_free_internal`,
  `heap_free_psram`, `rssi`, `priority_profile`, `req_jitter_ms`,
  `eff_jitter_ms`, `fill_frames` — populated in `main/audio_service.cpp`'s
  render-loop status block (core 1) from data the render loop already
  holds, plus a new `AudioEngineConfig::priority_profile` (a plain `uint8_t`,
  not the `PriorityProfile` enum — `neon/audio/types.hpp` cannot include
  `neon/config/model.hpp`, which includes it, without a cycle) carried
  cross-core through the existing seqlock rather than reading the raw
  `Config` global from the render task, which is not synchronized for
  cross-core reads.
- `hal::ILinkAudio::rx_high_water()` (`components/neon_hal/include/hal/
  ILinkAudio.hpp`), implemented in `link_audio_esp.cpp` and
  `link_audio_stub.cpp` — the `rx ring high-water` number that used to be
  log-only.
- `main/wifi.cpp`'s `neon_wifi_rssi()` — `esp_wifi_sta_get_ap_info()`,
  0 when not associated (AP-only mode included).
- A portable CSV formatter, `neon::telemetry_csv_header()` /
  `telemetry_csv_line()` (`components/neon_core/include/neon/telemetry/
  csv.hpp`, host-tested in `host/tests/test_telemetry_csv.cpp`), and the
  UART emitter itself in `main/audio_service.cpp`'s `audio_ctl_task`
  (core 0, 1 Hz), gated by the new `Config::telemetry_uart_csv` flag
  (`debug.telemetry_uart_csv` over REST) so it is off by default. Lines are
  prefixed `TEL,` to separate them from ordinary `ESP_LOG` noise on the same
  UART.
- `GET /api/scan` entries gained `channel`, for §6's channel histogram
  (`main/wifi.h`'s `NeonWifiScanEntry`, `main/wifi.cpp`).

### P5 (measurement rig) and execution tooling — did not exist; built this session

No RF-scan automation, config-sweep driver, or DAW-audio analysis script
existed anywhere in the repo. `tools/studio_mode/` (own README) now has:

- `rf_scan_logger.py` — §6's before/after RF characterization.
- `config_sweep.py` — the Phase 2 staircase driver, including setting
  `ap.policy` and `debug.priority_profile` per cell.
- `uart_telemetry_logger.py` — captures the P4 CSV stream to a file.
- `phase_error_analysis.py` — the P5 offline edge-detection / phase-error
  analysis (stddev, p99 |deviation from median|, max excursion, linear
  drift), checked against the plan's §5 pass criteria.

See `tools/studio_mode/README.md` for how these compose into each phase,
including the important caveat that `config_sweep.py`'s mid-run config
writes are fine for Phase 2 (counters only) and must **not** be reused for
Phase 1/3 (phase-error measurement — §6's "no web UI during runs" confound
exists specifically to keep a config-triggered SPI0 glitch out of that data).

### Net effect on this plan's schedule (§8)

Day 1's "P1, P2 (blocking fixes)" is now verification, not implementation —
budget accordingly. P3, P4, and P5's *build* work is done; what remains is
what the plan always intended for days 2-4: wiring up the physical rig,
running Phase 0 through 3, and writing up the result.
