# Index

Catalog of every wiki page. Read this first when answering a question, then
drill into the linked pages. See [README](README.md) for conventions.

## Boards
- [MaTouch](boards/matouch.md) — LinkSync MaTouch ESP32-S3 1.28" round
  GC9A01: capabilities (no jacks/codec), pin map (MIDI TX 43 / RX 44),
  MaTouch-specific UI, build/flash.

## Concepts (cross-cutting subsystems)
- [MIDI clock path](concepts/midi-clock-path.md) — clock out (pulse_task ISR
  vs midi_service), the playing-gate, external-clock follow, the S3 FIFO
  non-bug, M5 Unit-MIDI switch modes.
- [Wi-Fi provisioning](concepts/wifi-provisioning.md) — STA→AP fallback,
  persist-on-success, and the `netif` double-add reboot bug + fix.
- [Honest per-board menus](concepts/honest-menu.md) — trimming the shared
  `MenuModel` to what a board can actually do, via visible-row maps.

## Lessons
- [Debugging gotchas & dead-ends](lessons/debugging-lessons.md) — get the
  backtrace first; the five wrong theories; don't log from RT tasks; config
  persists on apply; NVS survives reflash; clean merges can be semantically
  wrong.

## Sessions
- [2026-08-27 — MaTouch settings, MIDI in/out, Wi-Fi robustness](sessions/2026-08-27-matouch-settings-midi-wifi.md)
  → squash `7e0a3dc` on `main`.

## Wanted (concepts referenced but not yet their own page)
- Config store / NVS lifecycle (`config_store.cpp`: apply vs apply_ram vs
  save vs flush, the G6 NVS hold, versioning) — referenced from wifi + lessons.
- The MIDI-PLL follower (`ClockSource::kMidiMaster`, PRs #41–45,
  `docs/SPIKE_MIDI_PLL.md`) vs the coarse UI-task follower.
- Board matrix (which of the ~8 targets have which hardware).
