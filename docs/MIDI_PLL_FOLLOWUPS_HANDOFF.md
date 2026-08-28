# Handoff — MIDI Clock Sync-In: Follow-Up Items

**Scope:** the small, near-term chores that finish what PR #40 landed
(`docs/SPIKE_MIDI_PLL.md` phase 1 + DIN/BLE plumbing + ESP32 wiring).
Everything here is bounded, mostly mechanical, and independent — pick any
item in any order. The larger remaining arc (bench acceptance, BLE
timestamp decoding, internal-timeline targets) lives in
`docs/MIDI_PLL_PHASES_HANDOFF.md`; do not duplicate work between the two.
**Prerequisite reading:** `docs/SPIKE_MIDI_PLL.md` (the design and its
status note), then the code it points at — the PLL and follower are short
and every policy decision is commented in place.

---

## 1. Restore CI, then compile-check the firmware targets  *(do first)*

GitHub Actions has been failing account-wide since ~2026-08-15: every job
completes in ~3 s with `runner_id: 0` — no runner is ever assigned, nothing
runs. Diagnosed on PR #39, reconfirmed with a re-run; the usual cause is a
billing/spending-limit block (GitHub **Settings → Billing**) or Actions
runner provisioning disabled for the account. This is an account setting,
not a repo fix.

Once runners come back:

1. Re-run the CI workflow on PR #40's head (or push any commit). The
   **host job is expected green** — the identical configuration
   (gcc, ASan/UBSan, `-Wall -Wextra -Werror`) passed locally with 393
   cases / ~291k assertions.
2. The jobs that could **not** be validated locally (no cross toolchains in
   the dev container) are the ones to watch:
   - ESP-IDF matrix (`main/midi_service.cpp`, `main/link_service.cpp`,
     `components/app_state/*` changed);
   - Daisy (`daisy/src/midi_daisy.cpp` — one-line parser call change);
   - Teensy (should be untouched: it uses no MIDI parser; a failure there
     means an include leaked).
   The firmware diffs were written against the exact surrounding idioms,
   but they have never been through a compiler. Budget one fix-up cycle.

Every merge since Aug 15 landed CI-unverified — when the queue drains,
check `main` goes green too, not just this PR.

## 2. Web editor: expose `clock_source = "midi"`

The firmware accepts and serializes `"midi"` (see
`components/neon_core/src/config_json.cpp`, `source_str` and the decode
branch), so the JSON API works today; only the editor UI can't select it.

- `web/src/api.ts:151` — widen the `ClockSource` type union.
- `web/src/routes/System.tsx:~153` and `web/src/routes/Outputs.tsx:~345` —
  the two `clock_source` selects need the new option (both surfaces show
  it; keep the wording consistent, e.g. `MIDI clock in`). Check for a
  shared options list first — if the two selects duplicate it, this is the
  moment to hoist it.
- `web/scripts/mock-device.mjs:53` and `web/vite.config.ts:42` — the mock
  configs carry `clock_source`; make sure the new value round-trips in the
  mock so the editor can be developed against it.
- **The bundle is committed.** The "Design system" CI job rebuilds and
  diffs the committed `.gz` (`.github/workflows/ci.yml`, "Web bundle is
  built and committed" step); after editing web sources run the same
  steps locally (`npm ci`, `npm run typecheck`, then the bundle build that
  step performs) and commit the regenerated artifact, or CI will fail on
  staleness.

## 3. Config JSON round-trip test for `"midi"`

`host/tests/test_config_json.cpp` exercises `clock_source` round-trips for
the original three values. Add the fourth: encode `kMidiMaster` → expect
`"midi"`, decode `"midi"` → expect `kMidiMaster`, and confirm
`config_sanitize` preserves it (the sanitize whitelist in
`components/neon_core/src/config_model.cpp` already includes it; the test
in `test_ext_clock.cpp` only covers the invalid-value → `kAuto` path).
Also worth one line: a **stored-blob downgrade** thought — an old firmware
reading a new blob with value 3 sanitizes to `kAuto`, which is the
intended graceful degrade; assert nothing, just don't "fix" it.

## 4. Status surfacing: MIDI vs CLK IN

`app_status_set_ext_clock(true)` now lights for *either* external source
(set in `main/link_service.cpp` from `follow_external || midi_following`).
The OLED and the web `StatusStrip` (`web/src/components/StatusStrip.tsx:49`,
`source_ext` vs `source_link`) therefore say "external" for MIDI too —
honest but undifferentiated. If the panel should distinguish
(`EXT` / `MIDI` / `LINK`), that's a third status value threaded through
`app_state` (`app_status_set_ext_clock` → a small enum), both UIs, and the
design-system status words (`design/`, regenerated via
`scripts/gen_design.py` — never hand-edit generated files). Cosmetic;
defer freely, but decide before the panel silkscreen conversation.

## 5. Suppress the router's duplicate transport path while following MIDI

Known, benign double-path from PR #40: with `transport_enabled` on, an
incoming 0xFA takes **two** routes — `MidiRouter` → `Sink::transport()` →
control queue → `kPlayNow`, *and* the sync tap → PLL → follower →
`set_playing(true)`. Same direction, so nothing misbehaves, but the
session gets two writes and the control-queue one is unquantized-by-design
while the follower's is edge-tracked. Cleanest fix: in
`main/link_service.cpp`, drop `kPlayNow`/`kStopNow` control commands while
`midi_act.following` is true (the follower is authoritative there), or
gate `Sink::transport()` in `main/midi_service.cpp` on
`app_status_ext_clock()`. Prefer the link-service side — it owns
arbitration. Add a host test to `test_midi_sync_follower.cpp`'s style if
the logic lands anywhere portable.

## 6. Documentation cross-links

The spike doc carries the status note, but the standing docs don't mention
the feature yet:

- `docs/ARCHITECTURE.md` — the "Bidirectional operation" section describes
  CLK IN only; add the MIDI path (tap → queue → follower → session) and
  the `kAuto` precedence.
- `SOFTWARE.md` §5 ("Bidirectional Logic") — same.
- `docs/FEATURES.md` and the README capability table — "MIDI clock sync-in
  (DIN + BLE)" is now a real differentiator vs the ML:2m; say so.
- `docs/DAISY.md` / `docs/TEENSY41.md` — their clocking sections should
  point at the phases handoff for the internal-timeline work rather than
  implying it exists.

## 7. Small code nits noticed after the fact

- `neon::MidiClockPll::on_start/on_continue/on_stop` take a `t_us` that is
  currently unused (kept for API symmetry). Either use it (validate
  ordering vs the next tick) or drop the parameters — decide once the BLE
  timestamp work (phases doc §B) settles whether transport bytes need
  their own times.
- `SyncEvent` is 24 bytes with padding; the queue is 64 deep (1.5 KB).
  Fine on ESP32-S3; re-check on the C3 target if the linksync-c3oled
  profile ever gains MIDI-in.
