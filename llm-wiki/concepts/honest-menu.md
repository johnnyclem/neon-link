# Concept: "Honest" per-board settings menus

The settings UI is one shared `neon::MenuModel` (`components/neon_core`,
host-tested). Every board renders the same items — but on a given board many
of those items drive hardware that isn't there (Eurorack jacks, audio codec,
CV, external clock in). Exposing them is a "setting that does nothing."

Pattern (implemented on MaTouch, `matouch_service.cpp`): keep `MenuModel`
untouched and add **board-local visible-row maps** that translate a visible
row position → the real `MenuModel` index. A null map means "show all".

- `kMenuVis` — which top-level sections appear.
- `kMidiVis`, `kSysVis` — which rows of a section appear.
- Cursor stays a real `MenuModel` index but is constrained to the visible
  set (`snap_cursor`, `settings_rotate`, `real_to_vis`/`vis_to_real`), so
  encoder rotate, touch taps, scroll, and highlight only traverse meaningful
  rows.

MaTouch result: **OUTPUTS** and **AUDIO** hidden (no jacks / no codec);
**MIDI** trimmed to `CLK OUT` (the one live row — BLE off, no CV for
CHANNEL/GATE/PITCH); **SYSTEM** keeps QUANTUM, MIDI NDG, SS SYNC, BRIGHT,
BEAT, COLOUR, VERSION, REBOOT and drops the pulse/clock-input timing rows
(LATENCY, RESET, SOURCE, IN PPQN, GATE CLK, RST EDGE, and STYLE once the
animations were removed).

Decision rule when trimming: a row stays only if it has an **observable
effect on this board** — drives an output that exists, changes the display,
or affects the Link session. When in doubt, ask the owner whether to remove
vs keep-as-future-surface (MIDI was kept as a wire-it-later surface, then
made live by assigning `kPinMidiTx`).

See [MaTouch board](../boards/matouch.md).
