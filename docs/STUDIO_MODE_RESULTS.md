# Studio Mode — bench results for the architect

**To:** author of `docs/STUDIO_MODE_TEST_PLAN.md`
**From:** JC bench, 2026-08-18
**DUT:** AMYboard / ESP32-S3, firmware `0.0.1-19-gc590035-dirty`
**Live:** Ableton Live 12 Suite, one Mac (`en0` 192.168.50.148 on `clemhaus`)
**Raw Phase 0 poll log:** `tools/studio_mode/phase0_d1_results.json`

This is discovery / session hold. It is **not** the P5 phase-error
baseline and **not** a Link Audio ship decision.

---

## Verdict in one page

| Gate | Result |
|---|---|
| Phase 0 — does Link multicast traverse SoftAP → one associated station? | **PASS.** Peer appeared immediately, held 10:00, no flap. Tempo had already crossed (Live 111 vs module `set_bpm` 120). |
| Phase 1 B-STA — Link-only on infrastructure, audio off, ≥30 min | **Session hold PASS (partial vs plan).** Peer 1, no flap, ≥37 min with I2S down. Two Live tempo changes observed (91 → 77; 115 was pre-mute). **P5 CLK1-vs-Ref WAV was not recorded.** `late_max_us` 4.025 s is a boot leftover, not this cell. |
| Phase 1 B-AP | **Not run.** |
| Phase 2 / 3 | **Not run.** No jitter staircase, no fullband audio, no 60 min soak. |

**Decision-matrix row:** Phase 0 did **not** fail. Studio Mode is not
killed by multicast. The four-day plan is still worth running. Do not
read this as “Cell B passes” — B in the matrix is STA **with audio and
the priority fix**, instrumented at CLK1.

---

## Architect notes on this report (same day)

Agreed: Phase 0 is the answer we came in for. Three extras, all taken.

1. **Ping 3.6 / 51.3 / 86.8 ms (24×) on an idle one-station AP.**
   Three packets; could be association settling. If real, it eats a
   60 ms jitter target and will look like Link Audio later. **Action:**
   100 pings the next time we are in AP mode (B-AP). Free.

2. **`late_max_us = 4.025 s` at uptime 144 s.** Not a stats-init
   artifact: `g_late_max_us` starts at 0, and the ISR records
   `now - due` on every fired edge. So at least one pulse edge was
   actually ~4 s overdue. Boot UART that same morning showed
   `external clock active` / `RST IN: anchoring downbeat` at 1.3 s,
   then `external clock lost` at 3.3 s — a plausible backlog dump
   into the GPTimer ring. **Action:** ISR no longer folds catch-up
   edges `> 200 ms` into `late_max` / `late_avg` (still counts
   `edges`). A real G7 miss stays in the counter (horizon is 67 ms).
   In tree, not on this DUT until the next flash.

3. **G6 vs network Save.** Working as designed and producing a
   confusing product: STA started in RAM, reboot reloaded
   `ap_policy=always`. A friend changing WiFi hits this. **Action:**
   `network_identity_changed` (SSID/pass/policy/SoftAP) now persists
   immediately even while I2S is holding. Same stall as a first-boot
   secret; better than a silent revert. In tree, not on this DUT.

**B-AP tonight** even without the P5 rig, for the macOS question:
Phase 0 held 10 min, but a dry-join hopped back in seconds. If macOS
will not stay on a no-internet AP for 30 min, that is a Studio Mode
blocker independent of Link.

---

## Phase 0 — SoftAP multicast gate

**Setup**

- Forced `ap_policy=always`. SoftAP `NEON-LINK-6BA0`, **open**
  (`ap_require_pass=0`, UART: `auth=open pass=(none)`), channel 1,
  AP-only at 192.168.4.1. Not `link1234`.
- One station: this Mac. DHCP `192.168.4.2`. UART:
  `station: be:06:cb:30:17:c1 join, AID=1`.
- Step 4 (unicast before Live): ping 3/3,
  rtt min/avg/max = 3.6 / 51.3 / 86.8 ms. `GET /api/status` HTTP 200.
- First `networksetup` join: `Could not find network NEON-LINK-6BA0`
  / `tmpErr`. Second attempt associated.
- I2S was **already up** (leftover Link Audio subscribe). Plan said
  “not audio”; we did not mute for this cell.

**Watch** (`2026-08-18T21:08:03Z` → `21:18:07Z`, 10 s poll)

| | |
|---|---|
| Module peers | **1** every sample (`peer_min = peer_max = 1`) |
| HTTP drops | 0 |
| bpm | 111.0 every sample (Live). Module `set_bpm` 120 |
| playing | false the whole watch |
| setup_ap | true, `ap_ssid=NEON-LINK-6BA0` |
| `network` field | `"none"` (STA not up; expected under Always) |

**Tempo / direction**

- Live → module: **yes** at join (111 vs 120). No mid-run tempo move
  during the 10 minutes (operator was on the AP; chat path was down).
- Module → Live: not scored (transport stopped, no operator nudge).
- Asymmetry: not observed. Both witnesses (Live Link button, module
  `/api/status` peers) showed 1.

**macOS**

- Did **not** hop off the AP during the 10 min watch.
- Earlier dry-join hopped back to `clemhaus` within seconds (no-internet
  / captive). Product problem, independent of Link.
- End-of-run `networksetup` restore to `clemhaus` failed (`-3900`).
  Mac stayed on 192.168.4.2 until a manual rejoin.

**Phase 0 call:** multicast SoftAP → station works and holds ten
minutes. Continue the plan.

---

## Phase 1 B-STA — infrastructure, audio off

**Setup (after leaving Always-AP)**

- `ap.policy=fallback`, joined `clemhaus`, STA IP **192.168.50.252**,
  `setup_ap=false`, OLED **STA**. RSSI −46 dBm at first LAN status.
- Audio: `enabled=false`, `sub_channel_id=""`, metro/publish off.
  I2S actually stopped: `/api/status` audio counters **froze** (fill_ms
  stuck at 388, `sub_state` stuck at `"playing"`). `audio.running` stays
  `true` because the idle path never publishes `running=0` — ignore that
  flag.
- Confound vs plan §6: we used HTTP `/api/status` during the run, not
  UART CSV. No `GET /api/scan` RF histogram. No P5 dual-channel WAV.

**Hold** (mute at uptime ~727 s; last good sample uptime **2969 s** ≈
**37 min** of audio-off Link)

| | Mute | Last sample |
|---|---|---|
| peers | 1 | 1 |
| playing | true (then mixed) | true |
| bpm (Live) | 91 | **77** |
| set_bpm | 120 | 120 |
| network | wifi / clemhaus | wifi / clemhaus |
| pulse edges | advancing | 309269, still advancing |
| late_avg_us | — | 436 |
| late_max_us | 4025110 | 4025110 (unchanged) |

Operator reported **two tempo changes** during the watch. Bench saw
Live tempo **115** (just after LAN join) → **91** (at mute) → **77**
(during hold). All Live → module. No peer flap.

**CLK1:** edges kept incrementing. `late_max_us = 4.025 s` was already
present at uptime 144 s (before this cell). Treat as boot / I2S-stop
residue, not B-STA sync error. `late_avg_us = 436` at the end is the
better live number and is **not** a P5 distribution.

**B-STA call:** session and tempo path on infrastructure hold for >30
min with audio off. **Do not use this as the Phase 1 phase-error
reference.** Next B-STA (or a continuation) needs the P5 rig.

---

## What this does not answer

- Phase error stddev / p99 / drift vs Ref (plan P5, §5).
- Whether AP **duty** costs sync vs STA (needs B-AP, same instrument).
- Whether H1 (topology) or H2 (priorities) buys Link **Audio**.
- Jitter floor (`la_jitter_ms ≤ 60`, `rx_dropped`, `jit_underruns`).
- `la_fullband` (filter was irrelevant; audio was off).

---

## Product notes that fell out of the bench (not the plan’s question)

1. **G1 is live.** This unit’s Always-AP was **open**. If Studio Mode
   ships, that is a ship blocker, not hygiene.
2. **Save vs Always-AP.** Saving `clemhaus` **did start STA** (RAM
   apply). It did **not** persist: I2S was up, G6 held NVS, reboot
   reloaded `ap_policy=always`. `wifi_identity_changed()` ignored
   `ap_policy`, so a policy-only Save also did not bounce STA. Patch
   is in tree, **not on this DUT**.
3. **`/api/status` `audio.running`** does not clear when I2S stops.
   Frozen counters are the tell.
4. **macOS will leave a no-internet AP** unless something holds the
   association. Captive-portal answer is still “record it, don’t fix
   tonight.”

---

## Recommended next (plan order)

1. **B-AP** — same Link-only, audio-off, 30 min, on `NEON-LINK-6BA0`
   (one station). Watch peers + tempo. Expect macOS hop; plan for it.
2. **P5 rig** — then repeat B-STA and B-AP as the actual baselines
   (impulse on bar 1, CLK1 divider, 96 kHz dual capture).
3. Only then Phase 2 staircase.

Do not start Phase 2 on HTTP peer-count alone.
