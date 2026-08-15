#!/usr/bin/env python3
"""Offline phase-error analysis for docs/STUDIO_MODE_TEST_PLAN.md's P5 rig.

Ref = interface output impulse, looped back to an input.
DUT = CLK1 through a 10:1 resistive divider into a line input.
Both channels recorded together at 96 kHz (10.4 us resolution) from one
live project firing a one-sample impulse on beat 1 of every bar.

Phase error = the distribution of (DUT - Ref) per downbeat, across the
run. Reports stddev, p99 |deviation from run median|, max excursion, and
linear drift -- the four numbers the plan's §5 pass-criteria table checks.
Absolute offset (fixed interface/DAC latency) is deliberately not reported.

Usage:
    python phase_error_analysis.py run.wav --bpm 120 --beats-per-bar 4
    python phase_error_analysis.py run.wav --bpm 120 --beats-per-bar 4 \\
        --baseline-p99-ms 0.40   # compare against a prior (e.g. B-STA) run's p99
"""
import argparse
import sys

import numpy as np
import soundfile as sf


def detect_edges(x, sr, threshold_frac, refractory_s):
    peak = np.max(np.abs(x))
    if peak <= 0:
        return np.array([], dtype=np.int64)
    thresh = threshold_frac * peak
    above = x > thresh
    rising = np.flatnonzero(above[1:] & ~above[:-1]) + 1
    if rising.size == 0:
        return rising.astype(np.int64)
    refractory_samples = int(refractory_s * sr)
    kept = [rising[0]]
    for idx in rising[1:]:
        if idx - kept[-1] >= refractory_samples:
            kept.append(idx)
    return np.array(kept, dtype=np.int64)


def align(ref_samples, dut_samples, sr, expected_period_s, tol_frac=0.5):
    """Greedy nearest-neighbor pairing: each Ref edge claims the closest
    unused DUT edge within tol_frac * expected_period. Edges are already
    roughly period-spaced, so per-edge matching (rather than a running
    anchor) survives a handful of misses on either side without derailing
    every pair after the miss."""
    tol_samples = tol_frac * expected_period_s * sr
    dut_used = np.zeros(len(dut_samples), dtype=bool)
    pairs = []
    missed = 0
    for r in ref_samples:
        candidates = np.flatnonzero(~dut_used)
        if candidates.size == 0:
            missed += 1
            continue
        deltas = np.abs(dut_samples[candidates] - r)
        j = int(np.argmin(deltas))
        if deltas[j] > tol_samples:
            missed += 1
            continue
        k = candidates[j]
        dut_used[k] = True
        pairs.append((r, dut_samples[k]))
    return pairs, missed


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("wav", help="stereo WAV: ch0=Ref, ch1=DUT")
    ap.add_argument("--bpm", type=float, required=True)
    ap.add_argument("--beats-per-bar", type=float, default=4.0)
    ap.add_argument(
        "--threshold-frac",
        type=float,
        default=0.3,
        help="fraction of each channel's peak amplitude counted as an edge",
    )
    ap.add_argument(
        "--refractory-ms",
        type=float,
        default=50.0,
        help="minimum spacing between accepted edges on one channel",
    )
    ap.add_argument(
        "--baseline-p99-ms",
        type=float,
        default=None,
        help="a prior baseline run's p99 |deviation|, for the §5 "
        "'within 1 ms of matched baseline' check",
    )
    args = ap.parse_args()

    data, sr = sf.read(args.wav, always_2d=True)
    if data.shape[1] < 2:
        print("expected a stereo file: ch0=Ref, ch1=DUT", file=sys.stderr)
        sys.exit(1)
    ref, dut = data[:, 0], data[:, 1]

    period_s = args.beats_per_bar * 60.0 / args.bpm
    refractory_s = args.refractory_ms / 1000.0
    ref_edges = detect_edges(ref, sr, args.threshold_frac, refractory_s)
    dut_edges = detect_edges(dut, sr, args.threshold_frac, refractory_s)
    print(
        f"detected {len(ref_edges)} Ref downbeats, {len(dut_edges)} DUT downbeats "
        f"(expected period {period_s * 1000:.1f} ms)"
    )

    pairs, missed = align(ref_edges, dut_edges, sr, period_s)
    if not pairs:
        print("no aligned downbeat pairs -- check the recording / --threshold-frac", file=sys.stderr)
        sys.exit(1)

    deltas_ms = np.array([(d - r) / sr * 1000.0 for r, d in pairs])
    median = np.median(deltas_ms)
    dev = np.abs(deltas_ms - median)
    stddev = float(np.std(deltas_ms))
    p99 = float(np.percentile(dev, 99))
    max_excursion = float(np.max(dev))

    # Linear drift: least-squares slope of delta vs. bar index, times the
    # run length -- "does the offset trend, not just wobble."
    t = np.arange(len(deltas_ms))
    slope, _intercept = np.polyfit(t, deltas_ms, 1)
    drift_ms = float(slope * (len(deltas_ms) - 1))

    print(f"downbeats analyzed: {len(pairs)}  (missed/unmatched: {missed})")
    print(
        f"phase error vs. run median: stddev={stddev:.3f} ms  "
        f"p99={p99:.3f} ms  max={max_excursion:.3f} ms"
    )
    print(f"linear drift across the run: {drift_ms:.3f} ms")

    ok = True
    if abs(drift_ms) >= 2.0:
        print("FAIL: |drift| >= 2 ms (§5)")
        ok = False
    else:
        print("OK: drift < 2 ms (§5)")
    if args.baseline_p99_ms is not None:
        delta = abs(p99 - args.baseline_p99_ms)
        passed = delta <= 1.0
        verdict = "OK" if passed else "FAIL"
        print(
            f"{verdict}: p99 vs. baseline ({args.baseline_p99_ms:.3f} ms): "
            f"delta {delta:.3f} ms (must be <= 1 ms per §5)"
        )
        ok = ok and passed
    if missed:
        print(
            f"note: {missed} downbeat(s) had no matching pair within tolerance -- "
            f"treat as a possible dropped/garbled pulse, not folded into the stats above"
        )

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
