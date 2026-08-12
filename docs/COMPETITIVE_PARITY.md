# Competitive Parity — Circuit Happy ML:2m / Missing Link Junior

**Status**: Active
**Sources**: *ML:2m Owner's Manual* and *The Missing Link Junior Owner's
Manual*, both firmware 1.5
**Related**: [`FEATURES.md`](FEATURES.md) · [`../SOFTWARE.md`](../SOFTWARE.md)

This document tracks NEON LINK against the two shipping products it
competes with, feature by feature, as documented in their manuals. It is
the checklist we hold ourselves to: **every row must be at least "match",
and the product case rests on the "beat" rows.**

Legend: ✅ match · 🔼 beat (we do more) · ⬜ not applicable

---

## 1. Playback / transport

| Capability | ML:2m · Junior | NEON LINK | |
|---|---|---|---|
| Ableton Link tempo + phase | yes | yes | ✅ |
| Link 3 start/stop sync | on/off switch | on/off, `start_stop_sync` | ✅ |
| Play / stop from the device | button | web editor, encoder, BLE MIDI | ✅ |
| Play/stop quantized to the loop | yes | yes (`TransportLatch`) | ✅ |
| Tap tempo | Tap button | `POST /api/tempo?op=tap`, run-aware averaging | ✅ |
| Tempo ±1 BPM | Up/Down buttons | nudge command, encoder | ✅ |
| Tempo ×2 / ÷2 | editor buttons | `op=double` / `op=half` | ✅ |
| Direct tempo entry | editor field | `POST /api/tempo?bpm=` | ✅ |
| Loop size (beats) | editor field | `quantum`, and it reaches the Link session | 🔼 |
| Delay compensation (CV) | ms field | `latency_us`, µs resolution | 🔼 |
| MIDI nudge | −10…+100 ms | `midi_nudge_us`, ±100 ms, µs resolution | 🔼 |
| Resync at next loop | Tap+Play | `POST /api/resync?op=next` | ✅ |
| Re-align the Link grid to now | Tap+Play (assignable) | `POST /api/resync?op=now` | ✅ |
| External clock **into** the session | — | CLK IN drives Link tempo; RST IN sets the downbeat | 🔼 |

## 2. Outputs

| Capability | ML:2m · Junior | NEON LINK | |
|---|---|---|---|
| Output count | 2 | 4 assignable + dedicated RESET + RUN | 🔼 |
| Clock mode | yes | yes | ✅ |
| Clock (always on) | yes | per-output `free_run` | ✅ |
| Gate while playing | yes | `role = gate` | ✅ |
| Reset every loop | yes | `role = reset_loop` | ✅ |
| Reset at clock start | yes | `role = reset_start` | ✅ |
| Reset at clock stop | yes | `role = reset_stop` | ✅ |
| Role is per output | 2 outputs, B also does MIDI | any of the 4 takes any role | 🔼 |
| PPQN | list | 1–192, plus ×1–16 / ÷1–16 rational rates | 🔼 |
| Trigger length | 2 / 5 / 10 ms | 0.1–100 ms, continuous | 🔼 |
| Square duty | 10 / 25 / 50 / 75 % | 1–99 %, continuous | 🔼 |
| Shuffle | yes (disabled at 24 PPQN) | 0–75 % at any rate | 🔼 |
| Reset aligned to the clock edge | on-edge / just before | `reset_before_edge` + `reset_lead_us` | 🔼 |
| Tempo CV out | — | 0–5 V, mappable BPM range | 🔼 |
| TRS MIDI clock out | yes | yes, with independent nudge | ✅ |

## 3. Rhythm Explorer

| Capability | ML:2m · Junior | NEON LINK | |
|---|---|---|---|
| Free assignment (click steps) | yes | `rhythm = pattern`, 64-step mask | ✅ |
| Euclidean (steps / pulses / rotation) | yes | `rhythm = euclid` | ✅ |
| Chance | yes | `probability_pct`, on every pattern mode | ✅ |
| Jitter | yes | `humanize_pct` | ✅ |
| Steps spread across the loop | yes | `rhythm_over_loop` | ✅ |
| Steps on the PPQN grid instead | — | default when `rhythm_over_loop` is off | 🔼 |
| Max steps | 16-ish (per the manual's graph) | 64 | 🔼 |
| Deterministic across peers/re-anchor | not stated | yes — patterns are a pure function of the absolute tick index | 🔼 |
| Independent pattern per output | 2 outputs | 4 outputs | 🔼 |

## 4. Local interface

| Capability | ML:2m · Junior | NEON LINK | |
|---|---|---|---|
| Display | 4-character (Junior) / none (ML:2m) | 128×128 OLED | 🔼 |
| Loop phase animation | button LEDs / display | phase bar + beat LED + editor loop meter | ✅ |
| Menu diving for deep settings | yes | encoder menu: outputs, roles, rhythm, settings | ✅ |
| Display brightness | yes | `display_brightness` 0–255, 0 blanks | ✅ |
| Tempo indicator | logo LED | beat LED | ✅ |
| Network status indicator | WiFi LED | net LED + editor status line | ✅ |

## 5. Networking

| Capability | ML:2m · Junior | NEON LINK | |
|---|---|---|---|
| 2.4 GHz WiFi | yes | yes | ✅ |
| Wired Ethernet | — | W5500, preferred over WiFi when present | 🔼 |
| Multiple stored networks | list | 4 slots, walked in order | ✅ |
| Attempts per network | "Try Each Connection" | `wifi_retries`, 1–10 | ✅ |
| Scan and pick a network | yes | `GET /api/scan` + editor pick-list | ✅ |
| Add a hidden network manually | yes | per-slot SSID entry + `hidden` flag | ✅ |
| Remove a stored network | yes | clear the slot | ✅ |
| AP mode | yes | yes | ✅ |
| AP: when to create | fallback / always | fallback / always / **off** | 🔼 |
| AP: SSID | yes | yes, defaults to `<DEVICE-NAME>-XXXX` | ✅ |
| AP: password / require password | yes | yes (under 8 chars → open, as WPA2 requires) | ✅ |
| AP: hidden SSID | yes | yes | ✅ |
| AP: channel | not exposed | 1–13 | 🔼 |
| mDNS `<name>.local` | yes | yes, and it follows a rename without a reboot | 🔼 |
| 192.168.4.1 fallback | yes | yes | ✅ |

## 6. Configuration and maintenance

| Capability | ML:2m · Junior | NEON LINK | |
|---|---|---|---|
| Web editor | yes | yes — the React app under `web/`, served pre-gzipped | ✅ |
| Firmware version display | yes | status line + FIRMWARE panel | ✅ |
| Firmware update | download from vendor server | `POST /api/ota`, upload a `.bin` | ✅ |
| Rollback on a bad image | not stated | inactive slot + `esp_ota_mark_app_valid` once the editor serves | 🔼 |
| Beta channel codes | yes | ⬜ — we ship the image directly, no vendor server | ⬜ |
| Factory reset | button combo | `POST /api/factory_reset?confirm=yes` | ✅ |
| Preset slots | — | 4, recallable by MIDI Program Change | 🔼 |
| Device naming | via AP SSID | explicit `device_name` driving hostname + AP SSID | 🔼 |

## 7. Things they do not have at all

- **BLE MIDI**: notes, CCs, and transport, routed to CV/gate and TRS.
- **Bidirectional sync**: an external clock can drive the Link session,
  not just follow it.
- **Ethernet**: a wired path with lower jitter than 2.4 GHz WiFi.
- **Tempo CV**: a control voltage proportional to session tempo.
- **Four independent outputs**, each with its own rate, shape, role, and
  rhythm pattern.
- **Rational clock rates**: `ppqn × mult ÷ div` with exact long-term
  phase, so 4 PPQN ÷ 3 emits four pulses every three beats forever
  without drift.

## 8. Deliberate non-goals

- **Beta codes / vendor update server.** Updates are a file upload; there
  is no fleet to gate behind a code.
- **The 4-character display idiom.** We have a real screen; the menu is
  designed for it rather than mimicking a 4-character crawl.

---

## Verification

The web editor exposes all of this through the design-system app in
`web/` (Live carries transport, Outputs carries roles and the step grid,
Network carries the stored list and access point, System carries the
remaining settings, the firmware upload and factory reset). The panel
reaches the same parameters through the encoder menu.

Everything in the "Playback", "Outputs", and "Rhythm Explorer" sections is
covered by host unit tests in `host/tests/` — `test_multi_engine.cpp`
(roles, reset modes, edge alignment, free run), `test_rhythm.cpp`
(patterns, chance, determinism), `test_transport.cpp` (tap tempo,
quantized transport, resync), and `test_config*.cpp` (persistence and the
editor's JSON contract). Run them with:

```bash
cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j && ctest --test-dir build-host --output-on-failure
```

## Note on flashing

Adding OTA changed the partition table from a single `factory` app slot to
`ota_0` / `ota_1`. The first flash after this change must be a full
`idf.py erase-flash flash`; incremental app-only flashes onto the old
layout will not boot.
