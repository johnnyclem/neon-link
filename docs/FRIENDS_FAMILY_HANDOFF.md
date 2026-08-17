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

### G2. I2S data width (review §C1)

**Symptom.** `i2s_audio.cpp` sets `data_bit_width` to
`I2S_DATA_BIT_WIDTH_16BIT` while `write_block` hands the driver hand-packed
`int32_t`. The hardware consumes 16-bit units from that buffer — alternating
zero/sample, scrambled channel assignment. Separately,
`I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG` leaves `bit_shift = true` while the
comment claims left-justified; `left_align` controls in-slot alignment, not
the WS-edge delay.

The metronome is the core feature of this product. This affects every unit.

**Fix.** Either set `data_bit_width` to 32-bit and keep the manual packing,
or drop the packing and pass `int16_t`. Match `bit_shift` to the PCM3060's
actual strapping.

**Verify.** Scope BCLK/WS/DOUT before listening. Full-scale 1 kHz sine: no
zero-alternation, no visible slew limiting. Do not troubleshoot this by ear.

### G3. SampleClock frame domain (review §C2)

**Symptom.** `audio_service.cpp` computes presentation time as
`us_at_frame(io.frames_written())`, but `SampleClock` is fitted against
`dma_mark()` reporting `isr_frames_`. With `auto_clear = true`, a starved
descriptor emits silence that increments `isr_frames_` with no matching
write. The offset between the two counters permanently shrinks, and beat↔
sample mapping shifts by up to 42 ms with no correction path — until reboot.

Pulse placement against CLK is the product. This silently breaks it.

**Fix.** On `write_block` failure, resynchronize:
`frames_written_ = isr_frames_ + ring_depth_frames`. Or derive presentation
frame from the driver's own queued count instead of maintaining two
counters.

**Verify.** Force a 100 ms audio-task stall. Phase recovers to baseline
within 2 s, measured at CLK on the scope.

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

Blocked on the Studio Mode plan (`studio-mode-test-plan.md`). Three
outcomes:

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
