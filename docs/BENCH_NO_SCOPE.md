# HANDOFF — bench session, no scope

**Scope of this doc:** the parts of G2, G3 and G6 that can be closed **today**
with a phone, a browser, and a UART console. No oscilloscope, no logic
analyzer.

**Device under test:** module on `clemhaus` at `192.168.50.252`, running the
G2/G3/G6/G7 image. Link real: 1 peer, 122 BPM, playing.

**Known-good baseline before you start** — record these, you are comparing
against them all session:

| | Value |
|---|---|
| Pulse | 7927 edges, `late_max` 38 µs, avg 4 µs |
| SampleClock | 2 ppm, no residual walk |
| I2S | running, 0 write failures, 0 underruns |
| LINE meters | peak L/R = 0 |
| Link Audio | not subscribed (`sub_state: idle`) |

**Order:** T1 → T2 → T3 → T4. T3 deliberately breaks things; do the passive
checks first.

**Not in this doc:** G2's `bit_shift` / slot-format verification, G4, G5, G7's
objdump. Those need tooling and stay open. See §5.

---

## T0 — order a logic analyzer first (2 minutes, do it now)

A cheap FX2-based 8-channel clone, $10–15, next-day. PulseView has a built-in
I2S decoder that prints slot width, channel assignment, and the decoded sample
values.

For "is `bit_shift` correct" a decoder that shows you the numbers beats a
scope trace you interpret by eye. This is the better tool for the job
regardless of budget, and you will re-run all of it on the P4 during bring-up.

Order it, then continue. Everything below runs while you wait.

---

## T1 — G2 pitch test (no equipment, decisive)

**What this proves.** If `data_bit_width` is 16 while `write_block` hands the
driver hand-packed `int32_t`, the hardware reads two 16-bit units per intended
sample. Content plays at **half rate** — exactly one octave down. That is
binary and unmissable. It does not require interpreting a waveform.

It does **not** prove `bit_shift` / slot format is right. That still needs
T0's analyzer. T1 closes the more damaging half of G2.

### Steps

1. Connect LINE OUT to headphones or a speaker. Set the AMYboard's 4 DIP
   switches for **line level** (all 4 the same way — check
   `docs/AMYBOARD.md`, the polarity is documented inconsistently upstream).
2. Command a **1 kHz tone** from the synth voice. Sustained, not a click —
   a transient will not give a tuner anything to lock onto.

   ```
   TOKEN=$(curl -s http://$DUT/api/config | python3 -c 'import json,sys; print(json.load(sys.stdin)["device_token"])')
   curl -s -X PUT http://$DUT/api/config \
     -H 'Content-Type: application/json' \
     -d '{"audio":{"amy_enabled":true,"amy_patch":2,"amy_gain":200,"role_l":"mix","role_r":"mix"}}'
   curl -s -X POST "http://$DUT/api/debug/note?n=83&vel=100&on=1" \
     -H "X-Neon-Token: $TOKEN"
   ```

   MIDI 83 is B5 ≈ 988 Hz. `amy_patch` 2 is the sine. `on=0` or `all=1`
   stops it.
3. Open any tuner app on your phone. Hold it to the speaker.

### Read

| Tuner shows | Verdict |
|---|---|
| **~1000 Hz (B5, slightly sharp)** | Packing correct. G2 half closed. |
| **~500 Hz (B4)** | **Packing wrong.** `data_bit_width` vs buffer mismatch confirmed. Fix before anything else — this affects every unit. |
| Neither, or unstable | Tone is not clean. Note it and continue; do not guess. |

An octave is not subtle. If the tuner reads an octave down, that is the bug,
not a measurement artifact.

### T1b — channel assignment (same setup, 30 seconds)

Pan the tone hard left. Confirm **nothing** comes out the right channel.
Repeat hard right. If both channels carry signal regardless of pan, the L/R
slot assignment is scrambled — the same root cause, same fix.

### T1c — meters sanity

While the tone plays, confirm the LINE meters move. Right now they read 0
because metro and mix are off, which is correct — but a meter reading 0
because it is broken looks identical to one reading 0 because there is no
signal. Confirm they track before you trust them anywhere else in this doc.

---

## T2 — G6 hold state and the transition (telemetry only)

The hold state is already confirmed: I2S is up, settings stay in RAM. That is
the easy half. **The transition is where the bug lives**, and it is untested.

### T2a — hold

1. Engine playing. Watch UART.
2. `PUT /api/config` — change any field from the web UI.
3. **UART must not print `config saved`.**
4. `GET /api/config` must show the new value (RAM applied immediately).
5. `pulse_stats.late_max_us` and `i2s_write_failures` must not move.

If `late_max_us` jumps by anything near a beat period, flash was written and
G6 is not holding.

### T2b — flush

6. Stop the engine. `audio.enabled=false` alone is **not** enough if
   `sub_channel_id` is set — that keeps `i2s_needed` and G6 held.
   Clear the subscribe id too (or unsubscribe in the UI).
7. Within ~2 s after I2S is actually down, UART **should** print
   `config saved`. Do not use `/api/reboot` as the power cycle — that
   path calls `flush_now` and is a false pass.
8. Power cycle (USB yank, RST, or esptool hard reset).
9. `GET /api/config` must show the value from step 2.

**Bench 2026-08-18:** T2b **pass** after the unsubscribe. First try
without clearing `sub_channel_id` lost the edit.

### T2c — the open question from the handoff (do this one)

The gate never answered: **what happens if a friend changes a setting and pulls
power without ever cycling the transport?**

10. Engine playing. Change a setting.
11. **Pull power immediately.** Do not stop the engine.
12. Power up. `GET /api/config` — is the change there?

**Either answer is acceptable, but it must be decided and documented.** If the
change is lost, that is a support ticket you cannot debug remotely, and the UI
needs to say "saves when you stop" somewhere visible. Write the result into
`HANDOFF.md` §1 G6 either way.

(Decided in firmware already: mid-play yank **may lose** the change; idle
edits persist. **T2c 2026-08-18:** yank after PUT `big_beat=false` while
playing; reboot had `big_beat=true`. Pass.)

---

## T3 — G3 forced stall (telemetry only, do this last)

**G3 is currently untested, not passing.** Zero underruns means the resync
path has never executed. That is an unexercised branch on a ship gate — the
classic works-on-my-desk-fails-in-six-weeks failure. Waiting for a natural
underrun in a quiet RF environment could take days and may never happen.

Force it.

### Method

`POST /api/debug/stall?ms=100` with `X-Neon-Token` (same header as OTA).
The audio task busy-waits that many milliseconds before the next
`write_block`, which is longer than the 42.6 ms DMA ring. Clamp is
20–250 ms; default 100.

```
TOKEN=$(curl -s http://$DUT/api/config | python3 -c 'import json,sys; print(json.load(sys.stdin)["device_token"])')
curl -s -X POST "http://$DUT/api/debug/stall?ms=100" -H "X-Neon-Token: $TOKEN"
```

UART prints `forced stall 100 ms`. `/api/status` `forced_stalls` must
increment — that is the tripwire that the audio task actually waited.
`i2s_write_failures` may stay 0: after a pre-write starve, `auto_clear`
leaves the DMA ring empty, so `write_block` succeeds inside its 43 ms
timeout and G3's success-path resync is what runs. A residual that parks
10–40 ms is a fail even if `forced_stalls` ticked.

### Steps

1. Record `clock_ppm`, residual, `late_max_us`, `i2s_write_failures` — the
   baseline table at the top of this doc.
2. Force the stall.
3. Confirm `i2s_write_failures` **ticks**. If it does not, the stall was not
   long enough — lengthen it. A gate you could not trip is not a gate you
   passed.
4. Watch `clock_ppm` and residual for 10 s.

### Read

| Result | Verdict |
|---|---|
| ppm and residual return to ~2 ppm / no walk within 2 s | **G3 passes.** Resync path works. |
| ppm settles but residual parks 10–40 ms off | **G3 fails.** This is exactly the `frames_written_` vs `isr_frames_` divergence. The fix did not land. |
| `late_max_us` jumps a beat period and stays | Pulse path affected too — that is G7, and it means the refill horizon is too short. Note it and raise G7. |

Repeat 3 times. The bug is a permanent offset that accumulates, so a single
recovery could be luck; three consecutive clean recoveries is evidence.

---

## T4 — record results

Append to `docs/FRIENDS_FAMILY_HANDOFF.md` §1 under each gate:

- **G2** — T1 tuner **988 Hz (B5)** 2026-08-18. T1b pan: left 128/0,
  right 0/128. `bit_shift` / slot format remains **open** pending the
  analyzer.
- **G6** — T2a/T2b pass or fail, and the **T2c decision** with the UI
  consequence if changes are lost.
- **G3** — three stall runs: ppm and residual before, after, recovery time.
  Pass or fail.

Raw numbers, not summaries. "G3 passes" without the ppm readings is not a
record.

---

## 5. Still open after today

| Gate | Blocked on |
|---|---|
| **G2** (slot format / `bit_shift`) | logic analyzer, T0 |
| **G4** (unsubscribe UAF) | 1000× subscribe/unsubscribe soak under active streaming |
| **G5** (serviceability) | version string, rollback, counters on System page — code, not bench |
| **G7** (refill horizon) | objdump on `on_alarm` **passed** (S3 + P4, 2026-08-17). Still open: first-beats-after-idle-commit (HANDOFF G7.3) |

G7 is worth flagging: G6 and G7 are **not independent**. Deferred commit is
what makes the refill horizon survivable; lengthening the horizon is what
makes a missed commit survivable. Do not ship one without the other.

---

## Do not

- Do not conclude G2 from listening for "a normal tick." The gate says do not
  troubleshoot this by ear, and a half-rate artifact on a click transient is
  subtle enough to pass a casual listen. The tuner test is the ear-free
  version — use a sustained tone and read a number.
- Do not mark G3 passed because no underruns have occurred. Untested is not
  passed.
- Do not run T3 before T1 and T2. It deliberately breaks the running state.
- Do not report a gate as closed without the raw numbers behind it.
