# Handoff: M5 Unit Encoder on AMYboard (Grove hub)

**Date:** 2026-08-24
**Hardware works.** The encoder is not dead. Twist always worked once the I2C driver existed. The same Unit Encoder is used on other Grove/UART setups without issue. Failures were in *this* firmware's click handling, I2C sharing, and (separately) a floating CV-in jack.

This note is for the next person (Claude or a human). Do not re-litigate polarity for 12 hours. Read the serial evidence first.

---

## Desired UX (product owner, explicit)

| Where | Twist | Single click | Double-click |
|---|---|---|---|
| **Live screen** (BPM / Link / status) | ±1 BPM | play / stop | **open settings menu** |
| **Settings menu** | move cursor | **enter highlighted row** | (not specified; treat as enter) |
| **BACK** (last row) | — | **return to live** | — |

**Boot must stay on the live screen.** Never auto-open settings. Never require unplug to leave a menu.

LIVE is also a menu row that currently means “go home” (same as BACK). That is a landmine: a leftover click on cursor 0 looks like “settings opened then immediately closed.”

---

## Hardware (bench)

- **AMYboard** ESP32-S3, neon-link firmware, USB-C.
- **Front Grove I2C:** SDA GPIO17, SCL GPIO18, `I2C_NUM_0` @ 400 kHz, mutexed (`halesp/i2c_bus.cpp`).
- **Passive 3-into-1 Grove hub** into the AMYboard Grove port.
- **OLED:** SH1107 @ **0x3c**, 128×128, I2C (working).
- **M5Stack Unit Encoder (U135):** STM32F030 I2C slave @ **0x40**. Official Arduino lib: `github.com/m5stack/M5Unit-Encoder`.
  - `0x00` mode (0 = pulse count)
  - `0x10` int16 LE encoder value (write-STOP-read)
  - `0x20` button, 1 byte (Arduino: `getButtonStatus()` → non-zero = pressed per comment; **resting bench read was `sw=1`**)
  - `0x30` SK6812 RGB
- **On the same bus:** GP8413 DAC @ 0x58, ADS1015 ADC @ 0x48.
- **No NULLLAB expander** @ 0x24 (probed nack).

Stock AMYboard has **no GPIO encoder pins** (`kPinEncA/B/Sw = -1`). Rotation *must* be I2C.

A GPIO-only Grove encoder cannot share the hub with the OLED (yellow/white would be A/B fighting SDA/SCL). This unit is I2C; display staying healthy on the same hub is expected.

Flash: `./scripts/flash_amyboard.sh` (ESP-IDF 5.3.2, 16 MB table). Opening USB serial with DTR/RTS can park the chip in ROM download; attach with DTR/RTS **deasserted**, RST only to boot. Port seen: `/dev/cu.usbmodem2101`.

---

## Serial evidence (2026-08-23 night, board idle on USB)

```
I2C probe OLED 0x3d=nack 0x3c=ACK ... exp 0x24=nack m5enc 0x40=ACK
panel128: SH1107 @ 0x3c
encoder: M5 Unit Encoder @ 0x40, count=0
ads1015: ADS1015 ready (ch0=1.20 V)
encoder: m5 val=0 sw=1     (repeating; rest)
link_svc: peers=0 tempo=121.00 playing=0
link_svc: external clock active: following CLK IN
link_svc: RST IN: anchoring downbeat
```

Then on a later boot (~7.8 s):

```
encoder: m5 click short raw=0    (t=7877 ms)
encoder: m5 click short raw=1    (t=8093 ms)   ← 216 ms later
encoder: m5 val=0 sw=1
encoder: m5 val=0 sw=0           ← stuck at 0 for the rest of the capture
link_svc: external clock lost
```

**Facts from that log:**

1. Encoder I2C ACK and pulse count work. Resting button bit is **1**.
2. Unconnected CV in 1 is **1.20 V**. Old CLK-IN path treated ≥ 1.0 V as a rising edge at first ADS sample → fake “following CLK IN” / RST downbeat. That is the “touch nothing, then big-number running mode” clue (plus `big_beat_display=1` when `playing`).
3. One mechanical click produced **two firmware shorts 216 ms apart** (press 1→0, release 0→1). Merge window was **180 ms**, so one click became a double-click (open settings).
4. After that, `sw` stuck at **0**. Rotation can still work because the STM32 accumulates the count; the **button is a level** and dies if I2C reads fail or stick. ADS1015 was polled every **1 ms** at FreeRTOS prio **8**; encoder task was prio **6**. Same mutexed I2C bus as OLED + GP8413.

There were **no** `m5 click short` lines during a quiet 8 s boot. Phantom *play* is more likely fake CLK IN (or Link) than a ghost encoder click. Phantom *settings* is more likely “one click = two shorts.”

---

## Intended mapping vs what shipped

`MenuModel` (`components/neon_core/src/menu_model.cpp`):

- Home rotate → `tempo_nudge_` (OLED pushes `kNudgeTempo`).
- Home **click** in the model = **open menu** (cursor 0 = LIVE = go home).
- OLED layer overloads that: live single click = play/stop (delayed 500 ms), live double-click = `open_settings()`.

Current OLED (`oled_ui.cpp`):

- 3 s `boot_locked()`: ignore presses; `go_home()` if not live.
- Live short: start 500 ms pending; second short / `kDouble` → settings.
- Live long: ignored (fake holds used to open settings).
- Not live: **any** short/double/long → `menu.on_click()`.
- `open_settings()`: `encoder_clear_press()`, open menu, **`set_cursor(1)` (OUTPUTS)** so leftover click is not LIVE→home, ignore presses 200 ms.

Current M5 task (`encoder_pcnt.cpp` `m5_encoder_task`):

- Poll 2 ms, prio 10.
- `0x10` pulse count, divide by **2** (STM32 counts both quadrature edges; 1 detent was 2 menu steps).
- Button: write-STOP-read `0x20`, bit 0. **Any 0↔1 change** after 3 stable samples is a short. Merge edges within **400 ms** into one short. **No long-press** from this path.
- `encoder_take_press()` still prefers `g_i2c_longs` over shorts if both are set (expander path).

CLK IN (`clkin_capture.cpp`): hysteresis rise **2.5 V** / fall **1.0 V**, poll **5 ms**, prio **3** (below encoder).

---

## What we tried (chronological, all in this firmware)

Do not repeat these as first moves.

### 1. No M5 driver at all
AMYboard only probed NULLLAB expander @ 0x24. Display worked; encoder did nothing. **Fix that stuck:** probe `0x40`, pulse count + button.

### 2. Raw pulse count as menu detents
1 detent = 2 menu items. **Kept:** `/ 2` with remainder (`kM5CountsPerDetent`).

### 3. Polarity from protocol PDF vs Arduino vs rest level
PDF: 0 released, 1 pressed. Rest on this unit: **`sw=1`**. Treating 1 as pressed made idle look held → fake long-press → open settings, then clicks never looked like clicks.

Tried: one-shot idle sample, 800 ms majority vote, “if idle looks held, invert,” relearn idle after 2 s down, emit on release, emit on press, `wait_idle` after open.

**`wait_idle` after open was catastrophic:** if idle was wrong, the FSM waited forever for a release that never matched → **clicks dead until unplug.** Do not bring that back.

### 4. Gesture pile-up
Stacked, often at once: boot mute, boot `go_home()`, long-press = menu, long-press ignored, double-click = menu, 500 ms delayed single-click = play/stop, 400–500 ms ignore after open, `encoder_clear_press()`, prefer long over short in `take_press` (which **drops shorts in the same poll**).

This is why it felt like 12 hours of mapping. The hardware click was never the slow part.

### 5. LIVE as row 0
Leftover edge after double-click → `on_click()` on LIVE → home. Symptom: “settings opens then closes in ~0.5 s.” 0.5 s also matched `kLongUs` when long-press on the menu meant back.

Workaround users found: **hold the second click** so the leftover release never fires; then stuck because the FSM thought the button was still down.

### 6. Duplicate `MenuModel::set_cursor`
Header already had `set_cursor` for touch/LCD. An inline duplicate was added; AMYboard **failed to compile** on flash. Removed; `set_cursor` lives in `menu_model.cpp` (clamped).

### 7. CLK IN false trigger
Unconnected jack ~1.2 V vs 1.0 V threshold. Hysteresis added (see above). Confirm after flash: should **not** log `external clock active` at boot with nothing patched.

---

## What still failed after the last flash (owner report)

- Can boot to live (good).
- Double-click can open settings (sometimes; one-click-as-two-shorts also opened it).
- **Cannot enter a submenu with a click.**
- **Cannot BACK / leave settings without power-cycle.**
- Untouched boot still went into “running / big numbers” (CLK IN and/or pending live short → play; `big_beat_display` default on).

Last serial session was **before** hysteresis + 400 ms merge + encoder prio 10. That last combo was **not** confirmed on hardware overnight.

---

## Likely remaining failure modes (check these first)

1. **I2C button reads fail or stick at 0 while the menu is up.** OLED flush + ADS1015 + GP8413 share the mutex. Count still works (STM32 accumulator). Log `m5 val= sw=` every 500 ms: if `sw` does not toggle on a physical click, the UI never sees it. Fix: even slower ADS poll, or skip ADS while no jack, or sample the button in the UI task immediately before/after `panel_flush`, or lower OLED flush rate on list screens.

2. **`take_press` still discards shorts when `longs > 0`.** If the expander path or leftover long bit is set, menu never gets `kShort`.

3. **200 ms ignore after open + 400 ms merge** can eat the first real menu click if the user clicks immediately.

4. **Pending 500 ms live short → `kPlayNow`.** A single edge after boot mute looks like play (“big numbers”). Owner then clicks to stop — proving **home click can work** when the button bit actually moves.

5. **Do not use long-press as “open menu” or as “back from top-level settings”** until the button level is trustworthy. BACK row is the exit. Long-press-as-back on `kMenu` is what auto-exited in 0.5 s.

---

## Files to read

| File | Role |
|---|---|
| `components/neon_hal_esp/src/encoder_pcnt.cpp` | M5 I2C encoder + click FSM |
| `components/neon_hal_esp/include/halesp/encoder_pcnt.hpp` | `EncoderPress`, `encoder_clear_press` |
| `components/neon_hal_esp/src/i2c_bus.cpp` | mutex, `i2c_write_stop_read`, 8-slot device cache |
| `components/neon_hal_esp/src/clkin_capture.cpp` | ADS1015 CLK/RST poll (bus hog + false trigger) |
| `components/oled_ui/src/oled_ui.cpp` | live vs menu mapping, boot lock, pending double-click |
| `components/neon_core/src/menu_model.cpp` | screens, LIVE/BACK, `on_click` / `on_rotate` |
| `components/neon_board/include/board_pins.h` | AMYboard: enc pins −1, I2C 17/18 |
| `docs/AMYBOARD.md` | I/O map |

Official encoder protocol/lib: `https://docs.m5stack.com/en/unit/encoder`, `https://github.com/m5stack/M5Unit-Encoder` (write-STOP-read, not repeated-START).

---

## Suggested approach (do this, not another polarity maze)

1. **Serial first.** Boot with DTR/RTS off. Confirm: no `external clock active` with open jacks; `sw` toggles 1↔0 on a physical click and does **not** stick; one mechanical click logs **one** `m5 click short`; a second click in settings logs `oled_ui: menu click cursor=`.
2. **If `sw` sticks at 0 after the first click:** I2C starvation or a bad 1-byte read. Probe: pause `clkin_ads` and see if clicks recover. Try `i2c_write_read` (repeated-START) only as fallback if STOP-read returns stuck zeros.
3. **If shorts fire but UI does not enter a row:** `handle_press` / `boot_locked` / `g_ignore_press_until_us` / `take_press` dropping shorts. Add a log at the top of `handle_press` with `ev` and `menu.screen()`.
4. **Keep mapping stupid:** live twist = BPM; live click = play/stop; live double-click = menu; menu click = `on_click`; BACK = home. No long-press on live. No long-press-as-back on the top-level list until shorts are proven.
5. **Do not** put “go home” on cursor 0 if leftover edges exist; BACK at the end is enough.
6. Compile (`idf.py build` with amyboard defaults) **before** asking anyone to flash. A duplicate `set_cursor` already wasted a flash cycle.

---

## Owner note

The encoder unit is known-good. Grove hub + SH1107 on the same bus is known-good. The unfinished job is: **one trustworthy click event from register 0x20 under OLED+ADS1015 traffic, then a 10-line mapping.** Everything else in this saga was compensating for a bad click.
