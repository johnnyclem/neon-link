# Studio Mode test tooling

Supporting scripts for `docs/STUDIO_MODE_TEST_PLAN.md`. Nothing here
replaces the plan — it's the P4/P5 infrastructure the plan calls for but
the firmware/repo didn't have yet, plus the drivers to run Phase 0-3
without hand-clicking through the web UI.

```
pip install -r requirements.txt
```

## What maps to what

| Plan section | Tool |
|---|---|
| P4 (UART CSV telemetry) | firmware-side: `main/audio_service.cpp`'s `audio_ctl_task`, gated by `debug.telemetry_uart_csv`. Capture it with `uart_telemetry_logger.py`. |
| P5 (measurement rig, offline analysis) | `phase_error_analysis.py` |
| Phase 2 (staircase) | `config_sweep.py` |
| §6 (RF characterization before/after each run) | `rf_scan_logger.py` |

## Firmware prerequisites

The telemetry fields and the priority-profile debug toggle this tooling
depends on were added alongside this directory:

- `AudioStatus` (`components/neon_core/include/neon/audio/types.hpp`) now
  carries `rx_high_water`, `i2s_write_failures`, `heap_free_internal`,
  `heap_free_psram`, `rssi`, `priority_profile`, `req_jitter_ms`,
  `eff_jitter_ms`, `fill_frames` — everything P4's CSV schema needs that
  used to be missing or log-only.
- `debug.priority_profile` (`"fixed"` | `"legacy"`) in `PUT /api/config`
  reproduces the pre-fix asio/pump priority inversion on demand, for Phase
  2 cells A/C — see `neon::PriorityProfile` in
  `components/neon_core/include/neon/config/model.hpp`. `"fixed"` is
  always the shipped, correct behavior; `"legacy"` exists only to make the
  regression checkable.
- `debug.telemetry_uart_csv` (bool) turns on the `TEL,...` CSV stream on
  the console UART. Off by default. The same flag also enables the
  per-tick `PLL,...` MIDI clock PLL stream
  (docs/MIDI_PLL_PHASES_HANDOFF.md Phase E) whenever MIDI clock is
  arriving; capture it separately with
  `uart_telemetry_logger.py --prefix PLL`.
- `GET /api/scan` entries now include `channel`, for the histogram in §6.

None of this needs a special firmware build — it's config, set through the
same `PUT /api/config` the web editor uses.

## Running a cell (Phase 2 example)

```bash
# Cell B: infrastructure mode, the shipped (fixed) priorities.
python config_sweep.py --host neon-link.local --cell B \
    --mode infra --priority fixed --out cell_b.jsonl

# Cell C: SoftAP, current/fixed priorities. Connect to the module's AP first.
python config_sweep.py --host 192.168.4.1 --cell C \
    --mode ap --priority fixed --out cell_c.jsonl

# Cell A / C's "current (pre-fix) priorities" comparison point:
python config_sweep.py --host neon-link.local --cell A \
    --mode infra --priority legacy --out cell_a.jsonl
```

`config_sweep.py` writes config mid-run (`PUT /api/config?persist=lazy`)
on every step change. That's fine for Phase 2, which only watches
`rx_dropped` / `jit_underruns` counters. **Do not** reuse that pattern for
Phase 1 baselines or the Phase 3 soak — see the confound rule in the
plan's §6 ("No web UI during runs") and the warning at the top of
`config_sweep.py`'s docstring. Those runs: set config once by hand
(or with one `PUT /api/config` before starting), then leave it alone.

## Running a measurement (Phase 1 / Phase 3)

1. Set config once (web UI or a single `PUT /api/config`), including
   `debug.telemetry_uart_csv: true`.
2. Start capture in parallel:
   ```bash
   python uart_telemetry_logger.py --port /dev/tty.usbmodemXXXX --out run.csv &
   python rf_scan_logger.py --host neon-link.local --run phase3-soak --when before
   ```
   Record the DUT/Ref audio in the DAW per the plan's P5 rig.
3. Run the full duration (30 min for Phase 1, 60 min for Phase 3).
4. Stop capture, then:
   ```bash
   python rf_scan_logger.py --host neon-link.local --run phase3-soak --when after
   ```
5. Analyze the recorded WAV:
   ```bash
   python phase_error_analysis.py phase3_soak.wav --bpm 120 --beats-per-bar 4 \
       --baseline-p99-ms <B-STA's p99, from its own analysis run>
   ```
   `run.csv`'s cumulative counters (`rx_dropped`, `jit_underruns`,
   `i2s_write_failures`) cover the rest of §5's pass criteria — diff the
   first and last row.

## Notes

- `phase_error_analysis.py` expects a stereo WAV, channel 0 = Ref
  (interface output looped back), channel 1 = DUT (CLK1 via the 10:1
  divider). It detects edges by amplitude threshold with a refractory
  period, then greedily pairs each Ref edge to its nearest unclaimed DUT
  edge — robust to an occasional dropped/garbled pulse on either side
  without derailing every pair after it. Unmatched edges are reported
  separately, not folded into the stddev/p99/drift numbers (a missing
  downbeat is a different finding than a mistimed one).
- `config_sweep.py`'s `mode` maps to `ap.policy`: `infra` → `off` (never
  self-host; join a stored network), `ap` → `always` (always self-host,
  never join). This is the same `ap_policy` the module already has —
  scripting it does not add a new mode.
- All three REST-driving scripts (`config_sweep.py`, `rf_scan_logger.py`)
  talk to whatever `--host` you give them (`neon-link.local` in
  infrastructure mode, `192.168.4.1` once you've joined the module's AP).
