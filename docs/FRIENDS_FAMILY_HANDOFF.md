# HANDOFF — friends & family batch (AMYboard)

**Platform:** AMYboard / ESP32-S3. **Not** the P4 — that's v2 R&D running in
parallel and nothing here depends on it.

**Context:** these are real units on other people's home networks, in other
cities. Every finding below is re-scored against that, not against a bench.

**Prior docs:** `docs/LINK_AUDIO_DEBUG.md`, `docs/AUDIOLINK.md`,
`HARDWARE.md` (10HP module — describes a different jack layout than the
AMYboard's fixed 5×2 block; see §5).

---

## 1. Ship gates — no unit leaves without these

### G1. Authentication (review §R1) — **highest priority**

**Symptom.** `ap_require_pass = 0` in `model.hpp` defaults the SoftAP to
open (`WIFI_AUTH_OPEN` in `net_manager.cpp`), and `web_ui.cpp` registers
`POST /api/ota` with no authentication check on any endpoint. Anyone within
RF range associates, uploads arbitrary firmware, reboots the module, and now
holds the owner's WiFi credentials from NVS.

This was tolerable on your desk. It is not tolerable in someone's house.

**Fix.**

1. `ap_require_pass = 1` by default.
2. Replace the `link1234` default with a per-device password derived from the
   MAC at first boot. Display it on the OLED under Setup.
3. Device token gating `POST /api/ota` and `POST /api/factory_reset`.
   Generate at first boot, store in NVS, show on the OLED. Sent as a custom
   header — which also closes CSRF, since custom headers aren't simple
   requests and can't be forged from a random web page on the same LAN.
4. `Host:` check or `Origin` allowlist on all mutating endpoints (DNS
   rebinding).

**Verify.** From a second machine on the AP: `POST /api/ota` without the
token returns 401. With the token, succeeds. A cross-origin form POST from a
local HTML file fails.

**Explicitly deferred:** secure boot v2 and signed OTA. Correct for a
production run, too much for this batch, and the token closes the remote
path. Write it in the v2 list.

### G2. I2S data width (review §C1) — **landed in tree**

`i2s_audio.cpp` packs stereo int16 into the top half of 32-bit slots and
opens the driver with `I2S_DATA_BIT_WIDTH_32BIT`,
`I2S_SLOT_BIT_WIDTH_32BIT`, Philips (`bit_shift = true`, one BCLK of WS
delay). That matches the PCM3060 hardware-mode default.

**Verify (still needed on the jack).** Scope BCLK/WS/DOUT. Full-scale
1 kHz sine: no zero-alternation, no visible slew limiting. Do not
troubleshoot this by ear.

**Bench 2026-08-18 — T1 (no scope).** DUT `192.168.50.252`, image
`0.0.1-19-gc590035-dirty`. Sine patch 2, MIDI 83 held via
`POST /api/debug/note`. Phone tuner: **988 Hz (B5)**. Not 494 Hz.
T1b meters: left solo peak 128/0, right solo 0/128. T1c mix peaks
128/128 (were 0). Packing and L/R assignment **pass**. `bit_shift` /
slot format still **open** (needs analyzer).

### G3. SampleClock frame domain (review §C2) — **landed in tree**

**Symptom.** `audio_service.cpp` computes presentation time as
`us_at_frame(io.frames_written())`, but `SampleClock` is fitted against
`dma_mark()` reporting `isr_frames_`. With `auto_clear = true`, a starved
descriptor emits silence that increments `isr_frames_` with no matching
write. The offset between the two counters permanently shrinks, and beat↔
sample mapping shifts by up to 42 ms with no correction path — until reboot.

**In tree.** `I2sAudio::write_block` resyncs on failure
(`frames_written_ = consumed + ring_depth`) and on the first successful
write after starvation (`frames_written_ < consumed`).

**Verify.** `POST /api/debug/stall?ms=100` (token-gated; see
`docs/BENCH_NO_SCOPE.md` T3). `forced_stalls` must increment (the audio
task consumed the wait). `clock_ppm` / `clock_residual_us` return to
baseline within 2 s. `i2s_write_failures` may stay 0: after a pre-write
starve, `auto_clear` leaves the ring empty so `write_block` succeeds and
G3's success-path resync (`frames_written_ < consumed`) is what runs.

**Bench 2026-08-18 (no scope) — T3.** DUT `192.168.50.252`, image
`0.0.1-18-g9841e15-dirty`, Link playing 120 BPM / 1 peer, I2S up.

| | baseline | run 1 100 ms | run 2 100 ms | run 3 250 ms |
|---|---|---|---|---|
| `forced_stalls` | 0 | **1** | **2** | **3** |
| `clock_ppm` | −3 | −3 | 3 | 3 |
| `clock_residual_us` | 10–18 | 14 | 17 | 19 |
| `i2s_write_failures` | 0 | 0 | 0 | 0 |
| `late_max_us` | 5 | 5 | 5 | 5 |

T3 **pass** (three consecutive recoveries; residual never parked 10–40 ms).
Pulse only emitted 3 edges the whole session — `late_max` is not a G7
reading on this run.

### G4. Unsubscribe use-after-free (review §C3)

**Symptom.** `link_audio_esp.cpp` deletes `source_` while `on_receive` may be
executing on Link's network thread; the `source_ == nullptr` check at entry
is a race, not a guard. `sink_destroy` uses `vTaskDelay(10ms)` in place of
synchronization, from a task at priority 5 that is routinely descheduled for
longer than that. `s.ring.reset()` races the audio task's `push()`.

A crash on someone else's unit that you cannot reproduce is the worst
possible bug class for this batch.

**Fix.** Real handshake — epoch counter or `std::atomic<bool> teardown` with
the callback setting an ack the destroyer spins on, or defer all destruction
to the pump task so the delete happens on a thread provably not inside the
callback. The sleep goes.

**Verify.** Loop subscribe/unsubscribe 1000× under active streaming, with a
mono/stereo flip every 50 iterations. No crash, no heap corruption.

**Note:** if Link Audio ships disabled (see §3), this drops to should-fix —
but only if the code path is genuinely unreachable, not merely off by
default.

### G5. Serviceability

You cannot support units in other cities without these.

- **Version string** visible on the OLED (System page) and in
  `GET /api/status`. Git SHA, not a hand-maintained number.
- **`esp_ota_mark_app_valid_cancel_rollback`** plus
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. Currently absent from the tree. A
  bad OTA on a remote unit with no rollback is a brick that has to be mailed
  back.
- **A written update procedure** for a non-engineer: join the module's AP,
  open the page, upload, wait. One paragraph, shipped with the unit.
- **A way to read the counters remotely.** `jit_underruns`, `rx_dropped`,
  `i2s_write_failures`, uptime, reset reason — on the System page. Your
  first bug report is otherwise unactionable.

### G6. Deferred NVS commit — **landed in tree**

A1 rev2 (closed 2026-08-17) on this AMYboard: a flash erase/write suspends
the other core for **`gap_max = 22.4 ms`**. The I2S DMA ring is
`8 × 256` frames at 48 kHz = **42.6 ms**. One NVS commit during playback
burns half the ring.

22.4 ms is a lower bound twice. Rev1 reported 5.4 ms because the
stressors never yielded and fewer erase cycles landed. Rev2 yields
between operations, so the figure is **one** erase-write, not a real
NVS commit (multiple sectors plus a page-table rewrite). Size anything
against the stall at **2–3× measured** (45–67 ms), not 22.4 ms plus a
hair. That band is the whole DMA ring.

`neon_config_apply` already wrote RAM and published the buses. That was
not enough: `neon_config_flush` committed 2 s after the last edit,
which is mid-session if the user stops twiddling and starts playing —
worse than committing immediately. Default `PUT /api/config` called
`neon_config_save` and erased flash in the httpd task. `persist=lazy`
was only the VST path.

**In tree.** `PUT /api/config` always `neon_config_apply`. I2S starts
only when `audio.enabled` or Link Audio pub/sub is on; the audio task
calls `neon_config_hold_nvs(true)` for that whole window. `neon_config_flush`
is a no-op while held and commits 2 s after I2S goes down (or after an
idle edit that never started I2S). `flush_now` still writes on reboot/OTA.

**Policy.** Apply to RAM instantly always.

- **Transport running** (I2S up and/or Link Audio subscribed): queue the
  blob. Flush on stop, on explicit reboot, and before OTA. A timer must
  not fire while DMA is running.
- **Transport stopped, including never started:** persist after the
  existing 2 s quiet window (or immediately on leave-page / reboot).
  First-boot secrets and factory reset stay immediate.

**Power loss.** A change made while stopped persists — yanking USB
after the web editor says saved, without ever hitting play, must
reload the new blob. That is G5: a friend follows “join AP, change a
setting, unplug.” A change made *during* playback that never sees a
stop is allowed to evaporate; the disk keeps the last committed blob.
Live RAM is what they heard. A silent evaporating *idle* edit is a
support ticket you cannot debug remotely. A lost mid-play tweak is
not.

**Exception — network identity.** SSID/password/`ap_policy`/SoftAP
fields persist even while G6 is held. Bench 2026-08-18: Save of
`clemhaus` started STA, reboot reloaded Always. A friend changing
WiFi networks would hit that. One NVS stall on a network Save is
better than a silent revert. Other mid-play edits still queue.

G6 and G7 are one pair. Deferred commit is what makes the refill
horizon safe. Lengthening the horizon (G7, 67 ms) is what makes a
missed commit survivable. Shipping one without the other leaves the
hole open.

**Verify.** Scope CLK1. `PUT /api/config` during playback must not move
`pulse_stats.late_max_us` or `i2s_write_failures`, and UART must not
print `config saved` until I2S is stopped. Change-while-stopped, wait
2 s, pull power, reboot: new blob. Change-while-playing, pull power
without stopping: previous blob, no clock glitch on the way down.

**Bench 2026-08-17 (no scope) — T2a hold.** DUT `192.168.50.252`, image
`0.0.1-18-g9841e15-dirty`, Link playing 122 BPM / 1 peer, I2S up
(`audio.enabled=true`, roles `link_in`/`link_in`, no subscribe).

| | before PUT | after PUT `big_beat_display: true` |
|---|---|---|
| `rev` | 2 | **3** |
| `big_beat_display` | false | **true** (GET /api/config) |
| `late_max_us` | 38–39 | **39** |
| `late_avg_us` | 4 | **4** |
| `i2s_write_failures` | 0 | **0** |
| `clock_ppm` | 2–3 | **1** |
| UART | — | `config updated from web editor (applied)` |
| UART `config saved` in 15 s after PUT | — | **absent** |

T2a **pass**. T2b (mute → `config saved` → power cycle) and T2c (yank
during play) not run.

**Bench 2026-08-18 — T2b flush.** Same DUT/image. First mute
(`audio.enabled=false` only) did **not** drop I2S: leftover
`la_sub_channel_id=78754242552f3441` keeps `i2s_needed=1`, so G6
stayed held and a hard reset lost the RAM edit (`big_beat` reverted
to true). Real stop is enabled-off **and** unsubscribe.

Retry: PUT `big_beat_display:false` while I2S up (`late_max` 0→6,
`i2s_write_failures` 0), then
`{"audio":{"enabled":false,"sub_channel_id":""}}`, wait 4 s, esptool
hard reset (no `flush_now`). After reboot:

| | NVS after reset |
|---|---|
| `big_beat_display` | **false** |
| `audio.enabled` | **false** |
| `sub_channel_id` | **empty** |
| `audio.running` | **false** |

T2b **pass**. Settings restored afterwards via PUT + `/api/reboot`.
`GET /api/status` `audio.running` stays stale-true until reboot if
I2S stops mid-session — do not use it as the mute tripwire.

**Bench 2026-08-18 — T2c yank.** Same DUT/image. I2S up, Link playing,
PUT `big_beat_display:false` (`rev` 4→5, `late_max` stayed 49,
`i2s_write_failures` 0). USB yanked 8 s later, no mute/stop. After
replug (`uptime` 7):

| | RAM before yank | NVS after yank |
|---|---|---|
| `big_beat_display` | false | **true** |

T2c **pass**. Mid-play edit evaporated. G6 hold + persist policy is
closed on this image.

### G7. GPTimer alarm path stays in IRAM — **landed in tree**

A flash stall in the pulse path is a missed beat on the clock output —
worse than any audio artifact.

`PulseHwGptimer::on_alarm` is `IRAM_ATTR`. Product `sdkconfig.defaults`
already sets `CONFIG_GPTIMER_ISR_IRAM_SAFE` and
`CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM`. `g_pulse_hw` is a global, so the
ISR `user` pointer is in internal RAM. That is the callback.

It is not the whole path. The core-1 refill task (`tasks_core1.cpp`)
runs from flash every 5 ms. A 15 ms horizon lost to a stall at the
sized number (45–67 ms, see G6) drained the edge ring and the ISR
parked. An IRAM callback with an empty queue still misses the beat.

**In tree.** `kHorizonUs` is 67 ms (3× A1 S3 `gap_max`). Callback was
already `IRAM_ATTR` with `GPTIMER_ISR_IRAM_SAFE`. G6 is what keeps flash
off this path.

**Fix (remaining).** Audit every call from `on_alarm` (including
`gptimer_set_alarm_action` error logs). Keep the object and `ring_` in
internal RAM. Do not ship this without G6, and do not ship G6 with the
horizon still at 15 ms.

**Verify.**

1. `objdump -d` on `on_alarm`: no fetch from `.flash.text`. Covers the
   callback, not the refill task. **Done 2026-08-17** on the G7 image
   (`build/neon_link.elf` S3, `build-p4v31/neon_link.elf` P4):
   - S3 `on_alarm` at `4037b8f8` in `.iram0.text`. Live `l32r` targets
     are the IRAM literal pool → DRAM atomics (`g_edges`, `g_levels`,
     `g_late_*`) or GPIO MMIO. `gptimer_get_raw_count` /
     `gptimer_set_alarm_action` / `__atomic_fetch_add_*` also
     `.iram0.text`. `g_pulse_hw` at `3fca10d8` (DRAM).
   - P4 `on_alarm` at `4ff21bec` in `.iram0.text`. `jal` helpers at
     `4ff23948` / `4ff239a0` (same section). BSS/atomics at `4ff3e3xx`
     (internal RAM). `g_pulse_hw` at `4ff34a90` (internal RAM). No
     `0x400xxxxx` flash loads.
2. With G6 held (I2S up): UART must not print `config saved` after a
   `PUT /api/config`. That is the hole that used to be open. **T2a
   passed** the same day.
3. After G6 lands: a forced NVS commit *while stopped*, then
   immediate transport start, must not show elevated `late_max_us` on
   the first beats. Refill has to be full before the first alarm.
   **Not run.** Bench Phase 0 saw `late_max_us = 4.025 s` at uptime
   144 s — a real `now-due`, not a zero-init. Boot that morning
   acquired then lost CLK IN (~1.3 s to 3.3 s). Catch-up edges
   `> 200 ms` are now excluded from `late_max` (in tree). G7.3
   (first beats after idle commit) still not run.

---

## 2. Should-fix before the batch

| # | Item | Why it matters here |
|---|---|---|
| §C4 | Task priorities: asio 12, `link_svc` 10, pump 9 | Link's timing protocol currently runs below audio TX. Cheap, and it's the leading hypothesis for the whole regression. |
| §C5 | Jitter target capped at 341 ms by `capacity_/2` | UI accepts up to 800 ms and silently truncates. Either resize the ring or clamp the UI to what's honored. |
| §C7 | Underrun forces full re-buffer | Converts one dropped packet into a full-target silence. Partial-refill hysteresis at ~25% of target. |
| §C8 | `g_packed` shared between read/write | Safe today by call ordering only. 4 KB landmine. Give each direction its own buffer. |
| §C10 | `channels()` reports rate 0 / channels 0 | Discovery UI shows "0 Hz". Looks broken to a friend. |
| §C9 | Pump task stack | 2 KB frame under lwIP's send path in 6144. Make the block buffer static; log high-water. |

---

## 3. Decisions pending

### D1. Does Link Audio ship, and in what state?

**Phase 0 (SoftAP multicast gate) — 2026-08-18, AMYboard.**
Raw observations, not a ship verdict.

- DUT: AMYboard, `0.0.1-19-gc590035-dirty`, `ap_policy=always`, SoftAP
  `NEON-LINK-6BA0` **open** (not `link1234`), channel 1, AP-only at
  `192.168.4.1`. I2S already up (leftover subscribe idle).
- Step 4: after associate, ping 3/3 (3.6–87 ms), `GET /api/status` 200.
  First `networksetup` join failed (`Could not find network` / `tmpErr`);
  second attempt got DHCP `192.168.4.2`. UART: `station … join, AID=1`.
- Peer: module saw **1 peer immediately**. Held **peers=1 for the full
  10:00**, no flap (`peer_min=peer_max=1`, 0 status drops).
- Tempo: module `set_bpm` 120, Live dictated **111.0** from the first
  sample and it stayed 111.0. So Live → module tempo already matched at
  join. No mid-run tempo change (nobody moved Live's tempo).
- Transport: `playing=false` the whole watch. Clock output not scored.
- Module `/api/status` peer count = 1 the whole time (second witness).
- macOS: **did not hop off during the 10 min watch**. Earlier dry-join
  did hop back to the house LAN within seconds (captive / no-internet).
  End-of-run restore to `clemhaus` failed (`networksetup -3900`); Mac
  stayed on `192.168.4.2` until a manual rejoin. Chat path is house Wi-Fi
  (`192.168.50.148`).
- Saving a LAN SSID from the editor **did start STA** (RAM apply). It
  did not stick across reboot: I2S was up, G6 held NVS, so the three
  Saves never printed `config saved`. Boot then reloaded `ap_policy=always`
  from flash. Editor Save vs Always-AP is still a real UX hole
  (`wifi_identity_changed` ignored policy until the follow-up patch).

Phase 0 multicast question: **peer discovery over this SoftAP works and
holds ten minutes.** Studio Mode is not killed by multicast.

**Phase 1 B-STA (same day, same DUT).** After leaving Always-AP:
STA `clemhaus` 192.168.50.252, OLED STA, audio `enabled=false` and
subscribe cleared, I2S down (status counters frozen). **peers=1 for
≥37 min**, no flap. Live tempo 115 → 91 → 77 (operator: two changes
during the watch). Pulse edges advancing, `late_avg_us` 436.
`late_max_us` 4.025 s is boot residue (already set at uptime 144 s).
**P5 CLK1-vs-Ref not recorded.** B-AP not run.

Full write-up for the plan author: `docs/STUDIO_MODE_RESULTS.md`.
The four-day plan is still worth running. Remaining product notes:
open AP (G1), macOS no-internet hop, editor Save vs Always-AP / G6.

Blocked on the rest of the Studio Mode plan (`studio-mode-test-plan.md`).
Three outcomes:

- **Passes** → ships, with the Stage/Studio mode split in the UI.
- **Marginal** → ships **off by default**, documented as experimental. G4
  stays a ship gate.
- **Fails** → cut from the batch. `CONFIG_NEON_LINK_AUDIO=n`, stub leg,
  hide the Publish/Subscribe sections of `Audio.tsx`.

The local audio engine — metronome, pulses-as-audio, synth voice, line-in
monitor — ships regardless. None of it touches the radio.

**Note:** if Studio Mode ships, G1 stops being hygiene and becomes
structural. Promoting the SoftAP from fallback to the primary audio path
makes the open-AP hole materially worse.

### D2. SPDIF jacks — **decided: leave them labeled SPDIF**

Both tips are AC-coupled through the PCM9211, not ESP32 GPIO. AMYboard
v1.4 schematic (`tulipcc/docs/pcbs/amyboard/amyboard-v1.4.sch`):

- **IN:** tip → 75 Ω to GND + ESD + **100 nF series** → PCM9211 `RXIN0`
- **OUT:** PCM9211 `MPO0` → **100 nF series** → 150 Ω → tip

A coupling cap makes DC clock / gate / Tempo CV impossible. No pin to
reassign, no second clock output, no extra input to protect. Print
`SPDIF` on the panel and leave the holes alone. Procedure and nets:
`docs/SPDIF_BENCH_TEST.md`. Jack-field copy no longer says `SOON`.

### D3. Batch size and date

Not yet set, and it determines whether the four-day plan runs before the
batch or Link Audio ships off-by-default with a later enable.

---

## 4. Explicitly out of scope for this batch

Write these down so they stop re-entering the conversation:

- ESP32-P4 port — v2 R&D, parallel track, informs nothing here
- USB Audio Class — if there's a cable to the Mac, an ES-9 does it better
- Secure boot v2 / signed OTA — v2
- MIDI host over USB-A — roadmap
- MIPI DSI display — v2 hardware only
- S/PDIF as S/PDIF — deferred since the original PRD, stays deferred

---

## 5. Panel and physical

The AMYboard has a **fixed 5×2 Thonkiconn block**, not the 10HP layout in
`HARDWARE.md`. `JackField.tsx` is the authority:

```
          IN        OUT
SPDIF     ·          ·
LINE      ·          ·
MIDI      ·          ·
CV 1      ·          ·
CV 2      ·          ·
```

Left column all inputs, right column all outputs.

- **Print row labels and column headers, not ten jack labels.** Five words
  down the left edge, `IN` / `OUT` across the top. The grid does the rest.
- **The panel labels the hole; the screen labels the signal.** `jackLive()`
  already resolves CV OUT 1 to TEMPO or PITCH, CV OUT 2 to the `clocks[0]`
  role, MIDI OUT to MCLK or MIDI. Static text can't track that and shouldn't
  try.
- **0.2 mm nozzle:** 2.5–3 mm cap height, 0.4–0.5 mm stroke, 0.3 mm deboss
  at 0.1 mm layers, counters ≥ 0.5 mm. Filament change on the top two layers
  beats paint-fill.
- **1 mm bezel lip over the OLED cutout** — the `GND VCC SCL SDA` silkscreen
  is currently visible through the window and reads as a defect.
- Label SPDIF normally even while deferred. An unlabeled hole looks like a
  manufacturing fault; a labeled inactive one looks like a roadmap.
