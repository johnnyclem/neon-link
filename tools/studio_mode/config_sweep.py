#!/usr/bin/env python3
"""Phase 2 staircase driver (docs/STUDIO_MODE_TEST_PLAN.md).

For a given mode/priority cell, steps la_jitter_ms down the ladder
200 -> 120 -> 80 -> 60 -> 40 -> 25, holding each step for --step-seconds
(default 300s = 5 min), and reports the lowest step that completed with
zero new rx_dropped and zero new jit_underruns.

This driver writes config mid-run (PUT /api/config?persist=lazy) on every
step change -- that is fine for THIS phase, which only watches counters,
not phase error. Do NOT reuse this pattern during Phase 1 baselines or the
Phase 3 soak: those runs capture phase error on a DAW, and the plan's own
confound rule (S6) says config must be set once, before the run, and left
alone there -- a config write mid-measurement is exactly the SPI0 glitch
that rule exists to keep out of the phase-error data.

Usage:
    python config_sweep.py --host neon-link.local --cell A \\
        --mode infra --priority fixed --out cell_a.jsonl

    python config_sweep.py --host 192.168.4.1 --cell D \\
        --mode ap --priority fixed --out cell_d.jsonl --step-seconds 60  # smoke test
"""
import argparse
import json
import sys
import time
from pathlib import Path

import requests

LADDER_MS = [200, 120, 80, 60, 40, 25]


def get_config(host):
    r = requests.get(f"http://{host}/api/config", timeout=10)
    r.raise_for_status()
    return r.json()


def put_config(host, patch):
    r = requests.put(f"http://{host}/api/config?persist=lazy", json=patch, timeout=10)
    r.raise_for_status()
    return r.json()


def get_status(host):
    r = requests.get(f"http://{host}/api/status", timeout=10)
    r.raise_for_status()
    return r.json()


def set_mode_and_priority(host, mode, priority):
    """mode: 'infra' (join a stored network, never self-host) or 'ap'
    (always self-host, SoftAP). priority: 'fixed' or 'legacy' -- see
    docs/STUDIO_MODE_TEST_PLAN.md Phase 2's cell table."""
    patch = {
        "ap": {"policy": "off" if mode == "infra" else "always"},
        "debug": {"priority_profile": priority},
    }
    put_config(host, patch)


def set_jitter_ms(host, ms):
    put_config(host, {"audio": {"jitter_ms": ms}})


def run_step(host, ms, step_seconds, poll_seconds):
    set_jitter_ms(host, ms)
    time.sleep(2)  # let the change land before the baseline read
    start = get_status(host)["audio"]
    print(
        f"  step {ms} ms -> req {start['req_jitter_ms']} eff "
        f"{start['eff_jitter_ms']} (baseline rx_dropped={start['rx_dropped']} "
        f"jit_underruns={start['jit_underruns']})"
    )
    if start["eff_jitter_ms"] != start["req_jitter_ms"]:
        print(
            f"  ! requested {start['req_jitter_ms']} ms but the ring "
            f"clamped to {start['eff_jitter_ms']} ms (§C5) -- this "
            f"step is not actually testing what its label says"
        )

    elapsed = 0
    deadline = step_seconds
    while elapsed < deadline:
        sleep_for = min(poll_seconds, deadline - elapsed)
        time.sleep(sleep_for)
        elapsed += sleep_for
        st = get_status(host)["audio"]
        print(
            f"    t+{elapsed:4d}s  rx_dropped +{st['rx_dropped'] - start['rx_dropped']}  "
            f"jit_underruns +{st['jit_underruns'] - start['jit_underruns']}"
        )

    end = get_status(host)["audio"]
    delta_rx = end["rx_dropped"] - start["rx_dropped"]
    delta_jit = end["jit_underruns"] - start["jit_underruns"]
    return {
        "requested_ms": ms,
        "req_jitter_ms": start["req_jitter_ms"],
        "eff_jitter_ms": start["eff_jitter_ms"],
        "rx_dropped_delta": delta_rx,
        "jit_underruns_delta": delta_jit,
        "clean": delta_rx == 0 and delta_jit == 0,
    }


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--host", required=True)
    ap.add_argument("--cell", required=True, help="A/B/C/D, or any label")
    ap.add_argument("--mode", choices=["infra", "ap"], required=True)
    ap.add_argument("--priority", choices=["fixed", "legacy"], required=True)
    ap.add_argument(
        "--ladder",
        default=",".join(str(x) for x in LADDER_MS),
        help="comma-separated ms values, descending",
    )
    ap.add_argument("--step-seconds", type=int, default=300)
    ap.add_argument("--poll-seconds", type=int, default=10)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    ladder = [int(x) for x in args.ladder.split(",")]

    print(f"[{args.cell}] mode={args.mode} priority={args.priority}")
    set_mode_and_priority(args.host, args.mode, args.priority)
    time.sleep(2)

    lowest_clean = None
    for ms in ladder:
        r = run_step(args.host, ms, args.step_seconds, args.poll_seconds)
        r["cell"] = args.cell
        r["ts"] = time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime())
        with args.out.open("a") as f:
            f.write(json.dumps(r) + "\n")
        if r["clean"]:
            print(f"  {ms} ms: clean")
            lowest_clean = ms
        else:
            print(
                f"  {ms} ms: FAILED (rx_dropped +{r['rx_dropped_delta']}, "
                f"jit_underruns +{r['jit_underruns_delta']})"
            )
            print(f"  stopping the descent at {ms} ms (first failure)")
            break

    if lowest_clean is None:
        print(f"[{args.cell}] no step in the ladder was clean")
        sys.exit(1)
    print(f"[{args.cell}] minimum viable jitter target: {lowest_clean} ms")


if __name__ == "__main__":
    main()
