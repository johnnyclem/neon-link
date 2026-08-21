# SPIKE PRD — neon-link on ESP32-C3 + 0.42" OLED

**Board:** ACEIRMC ESP32-C3 OLED SuperMini — ESP32-C3FN4/FH4, RISC-V
single-core 160 MHz, 4 MB flash, no PSRAM, ceramic antenna, 0.42" OLED,
25 × 20 mm.

**Status:** proposed spike. This is a **portability and floor-finding**
exercise, not a product path.

**Time box:** 2 days.

---

## 0. What this is actually for

Three questions, in order of value:

### Q1 — Does the neon-link core run on single-core RISC-V?

Everything shipped so far assumes dual-core Xtensa: a core-0/core-1 split,
Link and lwIP on one side, the refill task and render on the other. **The C3
has no second core.**

If Link + the timeline bus + the GPTimer edge ring run acceptably on one
RISC-V core, then the `link-sync` open-source target can name **the cheapest
ESP32 class in existence** as its reference board. That changes the pitch from
"buy an $8 XIAO" to "you probably already own one of these."

If it does not, that is equally valuable: it bounds the open-source story to
dual-core and we stop pretending otherwise.

### Q2 — Is 72 × 40 enough for the Nearby instruction card?

The `Nearby` PRD says the display's job when disconnected is to be an
instruction card: network name, password, next action. **This is the smallest
screen we would ever ship.** If the card is legible here, it is legible
anywhere.

### Q3 — What is the actual price floor for a Link node?

This board is roughly $4. It sets the bottom of the range for the
open-source target.

---

## 1. What this board cannot do — say it up front

- **No audio.** No codec, no I2S DAC, no PSRAM for buffers. Stem Sync is out.
- **No CV/gate at neon-link quality.** GPIO is there, but the analog
  conditioning is not.
- **4 MB flash.** Two OTA slots plus NVS leaves ~1.5–1.8 MB per app. Tight,
  and G5 requires OTA plus rollback.
- **2.4 GHz only**, ceramic antenna.

**This is a Link + MIDI + display node.** Which is exactly `link-sync`, and
nothing more.

---

## 2. Known gotchas — read before flashing

### 2.1 The OLED is offset, and will look broken if you miss it

0.42" panels of this class use an SSD1306-family controller addressing a
128 × 64 framebuffer while only **72 × 40 pixels are physically visible**,
with a column and row offset (commonly x≈30, y≈12).

Write at 0,0 and **nothing appears.** This is the single most common way an
evening disappears on these boards. Verify the offset empirically — draw a
full-screen border and see where it lands.

### 2.2 GPIO8 and GPIO9 are strapping pins

The pinout shows **GPIO9 = SCL, GPIO8 = SDA** for the OLED. On the C3:

- **GPIO9 is the boot-mode strapping pin** — held low at reset means download
  mode
- **GPIO8 must be high at reset** for normal boot

I2C pull-ups on these lines is standard for this board and generally works,
but if boot behaviour is erratic, this is the first place to look. Confirm the
board boots reliably from cold, 20 times, before trusting any other result.

### 2.3 UART assignment

GPIO20/21 are RX/TX (UART0, the console). **Do not put MIDI there.** The C3
has two UARTs and a GPIO matrix — assign UART1 to a free pin and fix it early.

---

## 3. Phases

### Phase 1 — display and boot sanity (2 hours)

1. Blink, confirm toolchain and flashing
2. 20 cold boots, confirm no download-mode lockups (§2.2)
3. Draw a border, find the real offset (§2.1)
4. Render "88.8 BPM" as large as the panel allows

**Exit:** BPM readable at arm's length in a lit room.

### Phase 2 — the memory and single-core question (half day)

Port the minimum: Link SDK, timeline bus, no MIDI, no display updates.

Record:

| Metric | Why |
|---|---|
| Binary size | vs the ~1.5–1.8 MB OTA slot |
| Free internal RAM after Link joins | 400 KB total, no PSRAM |
| Task CPU share on the single core | headroom for everything else |
| Peer discovery time | vs the AMYboard |

**Exit:** joins a Link session and holds tempo for 10 minutes.

### Phase 3 — timing under load (half day)

The real question. On one core, with Link and lwIP running, does the GPTimer
edge ring still place edges accurately?

1. Free-running `0xF8` at 24 PPQN out UART1
2. Logic analyzer on TX — measure the interval distribution
3. Repeat with Link active and a peer present
4. Repeat while the display is refreshing

**Step 4 matters:** an I2C display update is a blocking transaction on a core
that has nowhere else to run. This is the single-core penalty in its most
concrete form, and it is the thing the dual-core boards get for free.

**Exit:** `late_max` and the `0xF8` interval jitter, with and without display
refresh, both compared against the same measurement on a XIAO ESP32S3.

### Phase 4 — the instruction card (2 hours)

Render the `Nearby` disconnected state on 72 × 40:

```
JOIN WI-FI
NEON-LINK-6BA0
4f2a9c
```

**Photograph it and look at the photo from three feet away.** If the SSID
does not fit, shrink the font — never clip. A truncated network name is worse
than no display.

**Exit:** a photo, and an honest yes/no on legibility.

---

## 4. Pass criteria

| # | Criterion | Threshold |
|---|---|---|
| P1 | Cold boot reliability | 20/20, no download-mode lockups |
| P2 | Joins Link, holds 10 min | peer count stable |
| P3 | Binary fits an OTA slot | < 1.4 MB, leaving headroom |
| P4 | Free internal RAM after join | > 80 KB |
| P5 | `0xF8` interval jitter, Link active | within 2× the XIAO S3 baseline |
| P6 | Jitter **during display refresh** | within 3× baseline |
| P7 | SSID legible at 3 ft | yes/no, from a photo |

**P6 is the one to watch.** If a display refresh visibly disturbs MIDI
timing, the answer is not "abandon the C3" — it is "refresh the display
between beats, never during," which is a scheduling fix. But it needs to be
known.

**P3 is the quiet killer.** If the binary is already at 1.3 MB with Link and
no features, OTA plus rollback on 4 MB flash is not viable and the reference
board must have 8 MB.

---

## 5. Decision matrix

| Result | Consequence |
|---|---|
| All pass | **C3 becomes the `link-sync` reference board.** Open-source pitch is "$4 and a resistor." XIAO S3 becomes the recommended-quality option. |
| P5/P6 fail | Single core cannot hold timing under load. Reference board is dual-core; document the C3 as unsupported and say why. |
| P3 fails | 4 MB is not enough. Reference board needs 8 MB flash — the XIAO S3 or equivalent. |
| P7 fails | 0.42" is too small for the instruction card. Sets a minimum display size for any product that has one. |
| P1 fails | Board-specific strapping problem. Try another C3 board before blaming the chip. |

---

## 6. Explicitly out of scope

- Audio of any kind
- CV/gate
- Stem Sync
- BLE provisioning — hardcode credentials for the spike
- Battery
- Enclosure
- Any product decision beyond "is this a viable reference board"

---

## 7. Why this is worth two days

The `link-sync` open-source target's whole pitch is **"any ESP32-S3 and a
resistor."** If the C3 works, that becomes "any ESP32 and a resistor" — a
much bigger population, most of whom already own one.

And the display answer generalises: **72 × 40 is the floor.** Whatever fits
here fits everywhere, which settles the Nearby instruction-card design for
every future product without another argument.

Two days to either widen the open-source reach substantially or to bound it
honestly. Both outcomes are worth having written down.

---

## Appendix — status against the tree (2026-08-21)

Added when this PRD landed in the repo. Everything above is the spike as
proposed; this maps it onto what the firmware already has, what the two
days would actually be spent on, and which baselines the pass criteria
assume that do not exist yet.

### Q1 is sharper than the PRD states: RISC-V is already proven

The tree already builds and runs Link, the timeline bus, and the
ClockEngine on RISC-V: the ESP32-P4 targets
([LINKSYNC_P4LCD.md](LINKSYNC_P4LCD.md), [LINKSYNC_TAB5.md](LINKSYNC_TAB5.md),
[P4DEVKIT.md](P4DEVKIT.md)) are RISC-V, and lwIP runs on the P4 side of
ESP-Hosted. So the instruction set is not the open question. What the C3
uniquely tests is **one core at 160 MHz, 400 KB internal RAM, no PSRAM** —
the scheduling question (§3 Phase 3) and the memory question (§3 Phase 2),
not compilation or correctness.

### The mechanisms Q1 needs all exist and are portable

| Spike dependency | Where it lives |
|---|---|
| GPTimer edge ring | `components/neon_hal_esp/src/pulse_hw_gptimer.cpp` behind `hal/IPulseHw.hpp`. The IDF `gptimer` driver API is cross-chip and the C3 has the peripheral |
| MIDI clock scheduler | `neon::midi::ClockEngine` (`components/neon_core`) — portable, host-tested |
| Timeline bus | `SeqLock<TimelineSnapshot>` (`components/app_state/timeline_bus.h`) — no core-count assumptions |
| UART1 MIDI TX (§2.3) | `halesp::midi_uart_init(tx_gpio)` already takes an arbitrary pin, drives UART1 @ 31250, and writes the TX FIFO from the GPTimer ISR; `CONFIG_UART_ISR_IN_IRAM` is already in `sdkconfig.defaults.linksync` |
| `late_max` (§3, P5/P6) | `pulse_stats.late_max_us` / `late_avg_us` are already counted per edge and exported in the telemetry CSV — the pass criteria use existing instrumentation |

On §2.3 specifically: the linksync targets already put the console on
**USB Serial/JTAG**, not UART0 — on this board's native USB-C port that
carries over, so GPIO20/21 are not even the console. The rule stands:
MIDI goes on UART1 on a free pin, fixed in `board_pins.h`.

### Display: the render stack comes for free, the panel driver is new

The portable `Framebuffer` (`neon/gfx/framebuffer.hpp`) is already in
**SSD1306 page layout** — the same controller family as this panel — with
fonts, widgets, the hero font, and the menu model on top
(`components/oled_ui` renders it all on the AMYboard's 128×128 panels).
The new work for Phase 1 is a small SSD1306 72×40 init + flush that
applies the §2.1 visible-window offset; the buffer format already
matches. The existing drivers (`oled_ui/panel128.cpp`, EPD, LCD) are the
pattern to copy.

For Phase 4: the full §7.2 card in [NEARBY.md](NEARBY.md) is five text
groups — it adds `pass:` and `then open neon-link.local` to the three
lines quoted in §3 Phase 4. Render the **full** card; judge P7 on that,
not the abbreviated version.

### Porting work the phase plan does not list

- **Core pinning.** `main/tasks_core1.cpp` pins the pulse task to core 1
  and everything else to core 0. A unicore build has no core 1 —
  `xTaskCreatePinnedToCore(…, 1)` is invalid there. A core-id shim
  (core 1 → `tskNO_AFFINITY` on unicore) is small, mechanical, and
  required before Phase 2 boots at all.
- **Board plumbing.** A new `NEON_BOARD` Kconfig entry, a `board_pins.h`
  section, and an `sdkconfig.defaults.linksync-c3oled` — which must
  *drop* the SPIRAM and 8 MB-flash lines the XIAO defaults assume.
  Mechanical; six boards already show the pattern.
- **A 4 MB partition table.** None exists — `partitions.csv` is 8 MB
  (two 3 MB slots), `partitions_16mb.csv` is 16 MB. A two-slot 4 MB
  table yields ~1.5 MB per slot, which is where the P3 threshold of
  1.4 MB comes from. The flash scripts reject mismatched tables, so the
  new table has to be wired in, not just written.

### The baselines P3–P6 compare against do not exist yet

- **No recorded linksync binary size.** The 8 MB table's 3 MB slots mean
  nobody has had to watch it. P3 is measured against an unknown, and
  `sdkconfig.ci.bleoff` already exists as the size lever to try first if
  the number lands near the gate (the spike hardcodes credentials
  anyway, per §6).
- **No recorded free-internal-RAM-after-join number** on any target.
- **No XIAO S3 `0xF8` interval distribution.** The P5/P6 denominator has
  never been captured: [BENCH_NO_SCOPE.md](BENCH_NO_SCOPE.md)'s
  `late_max` 38 µs is the AMYboard, and it measures ISR placement
  lateness, not wire intervals on a logic analyzer. Phase 3 therefore
  includes the same bench pass on a XIAO S3 — budget for both boards on
  the analyzer, not one.

None of this changes the two-day time box's verdict value, but the first
half-day is board plumbing and baseline capture before any of §3's
questions get answered.
