# HANDOFF — Porting SolarOS findings into neon-link

**From**: the agent that built the RLCD target (PR #48, `docs/LINKSYNC_RLCD.md`)
and ran the SolarOS evaluation
**To**: the agent(s) implementing the three ports below
**Date**: 2026-08-29
**Source repo**: SolarOS = `github.com/johnnyclem/solar_os` (checked out at
`/home/user/solar_os` in the session that wrote this; line numbers are from
its `main` @ v4.10.0). File:line references below are load-bearing — they
were verified by reading the code, not guessed.

This is everything the evaluation found that is actually worth carrying
over, with the register values, algorithms, and traps inlined so you do not
have to re-mine SolarOS. Read it end to end before writing code. The three
ports are independent; each is its own branch + PR.

---

## 0. Scope

| Port | What | Targets touched | Verdict |
|---|---|---|---|
| **1. EPD fast refresh + region partials** | Unlock the SSD1683's 0xC7 fast mode and windowed partial updates | `linksync-epd` | Biggest UX change per line of code |
| **2. Idle power pass** | One brightness semantic, idle dim/blank, idle render throttling | MaTouch, P4LCD, Tab5, C3 OLED, 128×128 OLED | Fixes 4 divergent brightness meanings + the tree-wide "nothing ever dims" gap |
| **3. OSC control + telemetry** | UDP OSC in (tempo/transport) and out (tempo/beat/playing/…) | Every networked target | The missing external control surface; TouchOSC/lighting-rig ecosystem fit |

Smaller follow-ups (§5) can ride along with a related port or wait.
§6 lists what we evaluated and are deliberately **not** porting — do not
"improve" the firmware with those; the reasons are structural.

---

## 1. Ground rules (same as every port in this repo)

- **Portable logic goes in `components/neon_core`**, plain C++17, zero ESP
  includes, host-tested under `-Wall -Wextra -Werror` + ASan/UBSan. ESP
  glue goes in `components/neon_hal_esp` + a `main/*_service.cpp`. PR #48
  (RLCD) is the freshest precedent for the full wiring checklist; MaTouch
  commit `a87ac0f` is the older one.
- Host loop: `cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug`,
  `cmake --build build-host -j`, `ctest --test-dir build-host
  --output-on-failure`. New policy classes get doctest files registered in
  `host/CMakeLists.txt`.
- Firmware loop: `idf.py -B build-<v> -DSDKCONFIG=build-<v>/sdkconfig
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.<v>"
  set-target esp32s3 build` (ESP-IDF v5.3.2).
- **Watch the OTA slot limits**: the link-sync variants on 8 MB flash
  (`linksync`, `linksync-epd`) have **3 MB** slots, enforced in CI. Port 3
  adds code to every variant — check `build/neon_link.bin` size before
  pushing.
- **GitHub Actions has been dead repo-wide since ~Aug 24** — every job
  "fails" in 3 s with no runner assigned, on `main` too (documented on
  PR #48). Until the account's Actions spending limit / runner situation
  is fixed, local validation is the only validation. Do not chase those
  red checks as if they were yours.
- The real-time path is sacred: nothing you add may run above the pulse
  engine, touch its cores' timing, or enable light sleep / DFS (§6).

---

## 2. Port 1 — EPD fast refresh + region partials

### 2.1 Current state (what you are fixing)

`components/neon_hal_esp/src/epd_5in79.cpp` (Waveshare 5.79", 792×272,
**dual** SSD1683):

- `epd5in79_display(frame, fast)` takes a `fast` flag and **discards it**
  at lines 362-365: *"0xC7 is Waveshare Init_Fast() only (temp register
  load). Regular 0x12 + window + 0xC7 does not update the glass. Always
  0xF7."* Every full refresh is a multi-second flash.
- `epd5in79_display_partial()` (line 374) is mode 0xFF diffed against the
  old RAM — sub-second and non-flashing, but **always full-screen**:
  `window()` (line 156) writes hardcoded full-extent bounds and every
  partial re-streams all 26 928 bytes.
- `neon::EpdRefreshPlanner` (`components/neon_core/include/neon/ui/
  epd_refresh.hpp`) forces a full refresh every `kMaxPartials = 20`.

### 2.2 The fix SolarOS proves out

SolarOS drives a 400×300 single-SSD1683 CrowPanel with fast refresh as the
**steady state** (`/home/user/solar_os/src/drivers/epd_ssd1683.c`).

**Why neon-link's 0xC7 attempt failed**: 0xC7 needs the LUT loaded with a
temperature operand first. SolarOS's legacy init (`ssd1683_legacy_init`)
does, in order:

```
0x12                     SWRESET, wait BUSY
0x21 {0x40, 0x00}        display update control
0x3C {0x05}              border waveform
0x1A {0x6E}              write temperature register        ← the missing step
0x22 {0x91}              load temperature + LUT from OTP   ← and this one
0x20                     activate, delay 10 ms, wait BUSY
<address/window setup>
```

After that, the refresh trigger (`ssd1683_trigger_update`, line 572) is
simply:

```c
mode = partial ? 0xFF : (full ? 0xF7 : 0xC7);   // 0xC7 = FAST, works
cmd(0x22); data(mode); cmd(0x20); wait_busy();
```

**Ghosting policy** (`ssd1683_refresh`, line 596): auto mode runs 0xC7
and forces a 0xF7 full GC every `SSD1683_AUTO_FULL_INTERVAL = 20`
refreshes (`fast_refresh_count`); first refresh after any init is always
full. That "20" is already `EpdRefreshPlanner::kMaxPartials` — the
planner barely changes.

**Region partial updates** (gated to the Waveshare-V2 variant in SolarOS,
lines ~601-700): diff `buffer` vs `shadow` into a change window
`{x_start_byte, x_end_byte, y_start, y_end}` (X quantized to bytes), then:

```
0x11 {0x03}              data entry mode
0x44 {xs, xe}            X window (byte addresses)
0x45 {ys_lo, ys_hi, ye_lo, ye_hi}   Y window, 16-bit LE
0x4E {xs}  0x4F {ys_lo, ys_hi}      cursors
0x3C {0x80}; 0x21 {0x00,0x00}; 0x3C {0x80}
0x24 <windowed frame bytes>
0x22 {0xFF}; 0x20; wait BUSY
```

**Copy this verbatim — it is a bug fix, not style**: after the 0xFF
update, SolarOS re-writes **both** RAM planes (`0x26` then `0x24`) over
the same window so stale full-screen content cannot reappear on
alternating partials. And leaving partial mode into a full refresh forces
a complete controller re-init (line 613).

**Recovery**: if BUSY wedges >1 s after a reset, SolarOS re-resets with a
1000 ms low pulse (`SSD1683_RESET_RECOVERY_LOW_MS`). Cheap insurance;
`epd_5in79.cpp` has no equivalent today.

### 2.3 Dual-IC caveat (the one real design problem)

The 5.79" panel is two SSD1683s side by side. Master uses
`0x44/0x45/0x4E/0x4F/0x24/0x26`; slave mirrors on
`0xC4/0xC5/0xCE/0xCF/0xA4/0xA6` (see `write_planes()`, line 205, for the
byte split). A change window that crosses the seam must be issued twice
with per-IC X bounds.

**Recommended v1: Y-banded windows only** — full width, dirty row span.
It sidesteps the seam entirely, matches the row-span diffing that already
works on the RLCD target (`halesp::rlcd_st7305_present`), and the UI's
typical changes (BPM digits, cursor rows) are horizontal bands anyway.
Do X windowing later only if bench timing says it matters.

### 2.4 Planner + service changes

Extend `EpdRefreshPlanner` (host-tested, new doctest cases in
`host/tests/test_epd_refresh.cpp`):

- `Kind` gains `kFast`. Suggested policy: boot/wake → `kFull` (0xF7);
  layout change (overlay/invert flip) → `kFast` (0xC7 — non-flash full
  redraw instead of today's multi-second flash); content change →
  `kPartial` (0xFF, row-banded); every 20 non-GC paints → `kFull`.
- Driver API: give `epd5in79_display(frame, fast)` its `fast` flag back
  (0xC7 vs 0xF7 — both must seed the old RAM planes so partials can
  follow), and add
  `epd5in79_display_partial_rows(frame, row0, row_count)`.
- `main/epd_service.cpp` keeps its fingerprint/planner loop; it just maps
  the new Kind. The 25 s idle→sleep path is unchanged (sleep still
  discards the base; `awaken()` already handles re-seeding).

### 2.5 Bench protocol (do this before writing the planner)

1. Flash a build whose init adds only `0x1A {0x6E}` + `0x22 {0x91}` +
   `0x20`, then trigger one `0x22 {0xC7}; 0x20` refresh. **This is the
   whole experiment** — if the glass updates in <1 s without the black/
   white flash, the port is viable exactly as written. If not, try the
   V2-style path (skip the temp trick, rely on 0xFF+windows only).
2. Alternate 0xC7 and 0xFF refreshes 40× and photograph ghosting; tune
   the full-GC interval if 20 is wrong for this glass.
3. Verify a row-banded 0xFF partial on both halves of the panel and one
   spanning the seam.

Note SolarOS gates behaviors by a **variant autodetect** (BUSY-timing
probe after reset, `ssd1683_controller_init` line ~399) because the same
board ships with different controller revisions. If bench behavior is
inconsistent across your two known pin-maps (CrowPanel vs DevKit module),
that probe pattern is the fix.

---

## 3. Port 2 — Idle power pass (brightness, dimming, render throttling)

### 3.1 The mess being fixed

One config field, `neon::Config::display_brightness` (0-255,
`model.hpp:162`), means four different things today, and **no target has
idle dimming, blanking, burn-in mitigation, or an activity timestamp**:

| Target | Today | File |
|---|---|---|
| MaTouch | **Boolean** — GPIO45 on/off; the 0-255 BRIGHT menu row does nothing between 1 and 255 | `lcd_gc9a01.cpp:131-133`, `matouch_service.cpp:423-425` |
| P4LCD | ÷2.55 → STC8 expander PWM percent (I2C 0x2F, reg 0x20, **0-100 not 0-255**) | `lcd_rgb.cpp:46-72`, `lcd_service.cpp:475-477` |
| Tab5 | ÷2.55 → LEDC ch1, GPIO22, 5 kHz, 10-bit | `lcd_dsi.cpp:74-105` |
| 128×128 OLED | Raw 0x81 contrast; **0 = display off** + render skip — the only good one | `panel128.cpp:462-481`, `oled_ui.cpp:334` |
| C3 OLED | **Ignored entirely** — contrast baked into the init blob | `c3oled_service.cpp:80-83` |

Render loops are unconditional full repaints at fixed rates: MaTouch
~25 fps × 115 KB SPI, P4LCD ~30 fps composited, **Tab5 ~25 Hz × 1.8 MB
blit** (the most expensive idle frame in the tree), C3 ~12 fps. Only
`oled_ui` diffs frames — and uses it for beat anticipation, not idle.

### 3.2 What SolarOS contributes

- **Two-layer backlight state** (`tft_ili9341.c:~512-723`): a persisted
  user percent and a separate blanking gate; setting brightness while
  blanked stores it without lighting the panel; blanking couples the
  backlight to panel sleep (`0x28`/`0x29`); init keeps the backlight off
  until after `0x29` to hide the white flash (MaTouch already does the
  init half).
- **LEDC PWM at 20 kHz, 10-bit** for GPIO backlights
  (`pwm_port.c`) — flicker-free and above audio.
- Brightness as a first-class driver op with NVS persistence
  (`solar_os_display.c`, namespace `"display"`, key `"brightness"`).

### 3.3 Design

**Portable planner** `neon::ui::IdleDimmer` in `neon_core` (header-only
like `RlcdFramePlanner`, host-tested):

- Inputs: `note_activity(now)` (encoder/touch/button/web-command),
  `set_playing(bool)`, config `{dim_after_s, dim_level}`.
- Output: `Level { kActive, kDim, kBlank }` and a
  `frame_interval_hint_ms()` so services can throttle rendering.
- Policy: activity → `kActive`. Idle `dim_after_s` → `kDim`. Idle
  `3 × dim_after_s` while **stopped** → `kBlank` + render skip; while
  **playing** never blank (a running tempo box must stay glanceable),
  only dim. `dim_after_s = 0` disables the whole feature (default —
  behavior change must be opt-in).

**Per-target application**:

- **MaTouch**: convert GPIO45 to LEDC (low-speed, 10-bit, 20 kHz; any
  free timer/channel — nothing else uses LEDC on this target). Map
  0-255 → duty. Activity sources: encoder detent, touch, button. Add a
  diff-skip: hash the composed frame (or reuse a dirty flag from
  `snapshot()`) and skip identical 115 KB blits.
- **P4LCD / Tab5**: PWM already graded; wire `IdleDimmer` into
  `lcd_service.cpp` and honor `frame_interval_hint_ms` — Tab5 dropping
  from 25 Hz to ~2 Hz when dim is the single biggest win in this port.
- **C3 OLED**: add a runtime contrast write (`0x81, level` over the
  existing `halesp::i2c_write` path — the unified pattern is
  `panel128.cpp:462-481`), honor `display_brightness`, and blank via
  `0xAE`/`0xAF` at `kBlank`.
- **128×128 OLED**: mechanism exists; just feed `panel_set_brightness`
  from the dimmer (dim level, then 0 at `kBlank` — 0 already means
  display-off + render skip).

**Config**: add `display_dim_s` (u16, default 0) and `display_dim_level`
(u8, default 64) to `neon::Config`. Follow the existing
field-append/back-compat pattern in `config_model.cpp` / `config_json.cpp`
(the blob is versioned — see the v6 notes in `model.hpp:208`), surface in
`web/src/routes/System.tsx` next to the brightness slider and in
`menu_model.cpp`'s SYSTEM rows (respect MaTouch's `kSysVis` filter).

While here: fix the stale comment at `board_pins.h:381` ("CST816 not
driven by the POC" — touch has been live for a while).

### 3.4 Traps

- P4LCD's STC8 wants **percent**, not duty — clamp at 100
  (`lcd_rgb.cpp:52` already warns).
- Tab5's LEDC channel 1 / timer 0 are taken by the existing backlight —
  reuse them, don't double-configure.
- OLED contrast writes cost I2C — only write on change
  (`oled_ui.cpp:254-257` caches `applied_brightness`; keep that).
- Don't dim from an ISR or the render hot path; the dimmer ticks in the
  service loop like every other planner in this repo.

---

## 4. Port 3 — OSC control + telemetry out

### 4.1 Why

neon-link's only external control surface is the same-origin-gated REST
API (`web_ui.cpp:847-924`); telemetry never leaves the UART
(`telemetry_service.cpp` prints `TEL,` CSV at 1 Hz); and no MIDI CC can
set tempo (router roles are latency, 4× shuffle, CC123, PC→preset only —
`midi_router.cpp:59-89`). OSC fills all three gaps and is the native
language of TouchOSC / lighting rigs / show controllers this box will
live next to. Everything already funnels through one
`ControlCommand` queue, so OSC-in is a thin adapter.

### 4.2 What SolarOS contributes (semantics to replicate)

`/home/user/solar_os/src/services/solar_os_osc.c` (codec at lines
~301-576 is standalone: `esp_err_t` + `<math.h>` + `<string.h>` only) and
`src/jobs/solar_os_osc_job.c`. **License check before copying code
verbatim**: neon-link is GPLv2+; confirm `solar_os/LICENSE.md` is
compatible (both repos are the same owner's, but check). Everything
needed for a clean re-implementation is below.

- **Wire format**: hand-rolled OSC 1.0, big-endian, 4-byte aligned.
  Strings must have genuine NUL padding (reject sloppy encoders).
  Bundles accepted only with the immediate timetag `{0,1}`, recursion
  depth ≤ 2, ≤ 8 messages applied per packet, packet cap 512 bytes.
- **Inbound**: exactly one argument per message, typetags `,f ,i ,T ,F`;
  `f` is `isfinite`-checked; anything else rejected as
  `ESP_ERR_INVALID_RESPONSE`. Semantic failures (unknown path, bad
  value) are **counted, not errored** — only malformed encoding fails a
  packet.
- **Outbound bindings**: per-binding `interval_ms`, `delta`
  (compared against the last **sent** value, not last sampled — slow
  drifts still cross the threshold), `send_always`, and edge filters
  (rising/falling/both, never firing on the first observation).
  Two-phase commit: stash `pending_value`, promote to `last_sent_value`
  only after a successful `sendto` — a failed send never suppresses the
  retry.
- **Flood control**: 1-second tumbling window, 100 inbound packets/s
  max; optional single-peer IP filter.
- **Transport**: one `SOCK_DGRAM` socket for both directions, one worker
  task, `select()` with 10 ms timeout, target resolved once at start.

### 4.3 neon-link mapping

**Portable** (`neon_core`, host-tested): `neon/osc/codec.{hpp,cpp}`
(encode/decode + validation) and `neon/osc/bindings.hpp` (the
delta/edge/two-phase policy — pure logic, no sockets). doctest files:
codec round-trips, padding rejection, bundle depth, binding filter
semantics.

**Inbound address space** (all → `control_queue_push`, mirroring the
REST handlers' clamps — tempo through the same
`kMinMilliBpm/kMaxMilliBpm` double-space clamp as `web_ui.cpp:352-359`):

```
/neon/tempo      ,f BPM        → kSetTempo
/neon/nudge      ,i delta      → kNudgeTempo
/neon/transport  ,i|,T|,F      → kPlay / kStop   (1/T=play)
/neon/toggle     (any)         → kToggle
/neon/tap        (any)         → kTapTempo
/neon/resync     ,i (0 next/1 now) → kResyncNextLoop / kResyncNow
```

**Outbound v1 is a fixed binding table**, no registry UI:

| address | source | filter |
|---|---|---|
| `/neon/tempo` | milli_bpm/1000.0f | delta 0.05, 250 ms |
| `/neon/playing` | transport | edge both |
| `/neon/beat` | beat number | event, playing only |
| `/neon/peers` | peer count | delta 1, 1 s |
| `/neon/battery` | RLCD only, volts | 60 s |

**ESP side**: `main/osc_service.cpp` (new), one UDP socket, task on core
0 at priority ≤ 3 (strictly below the Link asio priorities —
`ablink/priority.hpp`). Config fields: `osc_enabled` (**default 0** —
this is an unauthenticated UDP surface; the same philosophy as
`check_local_origin`, so it must be opt-in), `osc_listen_port` (default
9000), `osc_target[..]` ("host:port" for outbound; empty = in only).
Surface all three in the web System page; document LAN-only in
`docs/OSC.md` (new, short).

### 4.4 Traps

- **Socket budget**: `CONFIG_LWIP_MAX_SOCKETS=16` shared with Link's
  asio, neon_sync's `*:20809` multicast, mDNS, and httpd. One more
  socket fits; don't open per-send sockets.
- **Image size**: this lands on every networked variant including the
  3 MB-slot ones — check `neon_link.bin` on `linksync` before pushing.
- Do not dispatch OSC on the recv thread into anything blocking; the
  queue push is non-blocking by design.
- Tempo floods: rely on the 100 pkt/s window **and** let the Link
  service's own command handling coalesce — do not add sleeps.

---

## 5. Smaller follow-ups (optional riders)

- **RLCD runtime tuning** (rider on any RLCD work): the ST7305 numbers I
  shipped are compile-time (`HPM 32 Hz / LPM 1 Hz / 3 s idle`). SolarOS
  persists them in NVS namespace `"rlcd_st7305"`, keys `idle_lpm_ms`,
  `lpm_hz`, `hpm_hz`, `power`, `inverted`, with the rate tables:
  HPM label→(OSCSET byte0, HFRA bit): 16→(0xA6,0), 32→(0xA6,1),
  25.5→(0x80,0), 51→(0x80,1); LPM 0.25/0.5/1/2/4/8 Hz → LFRA 0-5
  (FRCTRL `0xB2`: LFRA mask 0x07, HFRA 0x10). Write NVS **before**
  applying to hardware so failed applies keep the intent.
- **Battery upgrade** (RLCD; later XIAO's reserved GPIO2 divider pad):
  replace the hard-coded percent curve with NVS-configurable rails
  (defaults 3000/4120 mV), an 8-sample boxcar, and SolarOS's two-signal
  charge detection — instantaneous `mv > max_mv` ⇒ external power, plus
  a least-squares slope over the last 8 of 16 samples with **±10 mV/h**
  thresholds ⇒ CHARGING/FLAT/DISCHARGING and
  `time_left_min = (mv - min_mv)·60 / |slope|`. Also factor the RLCD
  `Battery` class out of `rlcd_service.cpp` into `halesp` so other
  boards can use it.
- **PCF85063 idle clock face** (RLCD): the board has the RTC on I2C
  (addr 0x51) and the panel holds an image at µA in LPM — a free
  bedside clock when idle. SolarOS driver notes: CTRL1 0x00 (STOP 0x20,
  12H 0x02), probe via RAM byte 0x03, burst-read 7 bytes from SEC 0x04,
  OS bit 0x80 in seconds = oscillator-stopped (invert into a validity
  flag and refuse to show an invalid clock), compute weekday yourself
  (Zeller), bracket writes with init.
- **CC→tempo**: a small, well-localized add at `midi_router.cpp:59-84` —
  but a single 7-bit CC is too coarse for BPM; implement it as a
  **nudge** CC (relative, 64-centered like `cc_latency`) or a 14-bit
  MSB/LSB pair, never an absolute 0-127→BPM map.
- **`/api/status` gaps**: while touching services, add `battery_mv` /
  `battery_pct` (RLCD) and `brightness`/`dim` state — the web editor and
  OSC out both want them, and telemetry CSV has neither.

---

## 6. Evaluated and rejected — do not port these

- **SolarOS power management** (CONFIG_PM, tickless idle, light sleep,
  DFS profiles, BT modem sleep). Right for a pocket terminal, wrong
  here: light sleep and frequency scaling destroy the pulse engine's
  jitter floor and Link/WiFi timing, and `CONFIG_FREERTOS_HZ=1000` is
  load-bearing for the 1 ms scheduling this firmware assumes. The
  transferable *idea* — spend less when idle — is Port 2's render
  throttling, at the service-loop level, never the CPU level.
- **Task admission / memory-role framework**. neon-link's static,
  core-pinned task set with explicit priorities is *ahead* of SolarOS
  here (SolarOS runs everything within 3 priorities of idle, unpinned,
  below WiFi). Adopting its admission machinery is churn without
  benefit at this scale.
- **u8g2**. The host-tested canvas/renderer stack is a strength; keep
  it. (Port 1 deliberately re-expresses SolarOS's u8g2-shaped e-paper
  logic against `EpdCanvas`, the same way the RLCD port did for the
  ST7305.)
- **Board-manifest generation**. Elegant, but `board_pins.h` +
  Kconfig defaults already serve ten targets fine; a generator is a
  migration, not a port.
- **SolarOS MIDI**. Nothing to take — its transport/message coverage is
  a subset of neon-link's (no clock, no sync, no BLE). Its *binding
  model* inspired Port 3's shape, which is where that value lands.

---

## 7. Definition of done, per port

Every port: host suite green (`ctest`, with new doctest coverage for any
new planner/codec), the affected variant(s) build under ESP-IDF v5.3.2,
image fits its OTA slot, docs updated (`LINKSYNC_EPD.md` refresh-policy
section for Port 1; a brightness/dimming paragraph per target doc for
Port 2; new `docs/OSC.md` for Port 3), README table rows where a new
user-visible feature appears, and CI matrix untouched unless a variant is
added. Hardware-in-the-loop evidence for Port 1 (the §2.5 photos) before
declaring fast refresh real — a successful compile is not hardware
validation, and the 0xC7 behavior is exactly the kind of thing that
differs between panel revisions.
