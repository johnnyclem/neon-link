#!/usr/bin/env python3
"""S1 Phase A — lookahead distribution and pass/fail.

Reads daemon.py CSVs and prints, per file and pooled:
  - the lookahead_ms distribution (min / p10 / median / mean / max)
  - an ASCII histogram — the A1 histogram is the headline deliverable
  - the §4 pass criteria it can compute mechanically (P1–P4)

P5 (per-clip override) and P6 (Live 11 vs 12) are judged by the human
from the A5 / A9 files. This script does not say "passed" on its own;
paste its output with the CSVs.

stdlib only. --png writes a matplotlib histogram if matplotlib exists.
"""

import argparse
import csv
import math
import sys

# Raw prediction error is measured at *receipt* of the playing event, so
# it includes delivery latency. P2 asks whether the predicted grid point
# is the one playback actually started on: round the observed beat to
# the quantum grid and compare. Tolerance covers float noise only.
GRID_TOL_BEATS = 0.05


def load(path):
    fired, playing = [], []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if r["event"] == "fired":
                fired.append(r)
            elif r["event"] == "playing":
                playing.append(r)
    return fired, playing


def fnum(row, key):
    v = row.get(key, "")
    return float(v) if v not in ("", None) else None


def percentile(sorted_vals, p):
    if not sorted_vals:
        return None
    k = (len(sorted_vals) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    if lo == hi:
        return sorted_vals[lo]
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def histogram(vals, bin_ms=100, width=50):
    if not vals:
        return "  (no data)"
    top = max(vals)
    nbins = int(top // bin_ms) + 1
    bins = [0] * nbins
    for v in vals:
        bins[min(int(v // bin_ms), nbins - 1)] += 1
    peak = max(bins)
    lines = []
    for i, n in enumerate(bins):
        bar = "#" * max(1 if n else 0, round(n / peak * width))
        lines.append(f"  {i * bin_ms:5d}-{(i + 1) * bin_ms:<5d} ms |{bar} {n}")
    return "\n".join(lines)


def analyze_file(path):
    fired, playing = load(path)
    look = sorted(v for v in (fnum(r, "lookahead_ms") for r in fired)
                  if v is not None)

    matched = [r for r in playing if "matched_fired" in r.get("notes", "")]
    unmatched = [r for r in playing if "no_pending_fired" in r.get("notes", "")]

    # P1: fired observed before the actual start. A launch that produced a
    # playing event with no prior fired event counts against it.
    p1_ok = sum(1 for r in matched if (fnum(r, "lookahead_ms") or 0) > 0)
    p1_total = len(matched) + len(unmatched)

    # P2: predicted grid point == observed start rounded to the grid.
    p2_ok = p2_total = 0
    p2_bad = []
    for r in matched:
        pred, actual, q = (fnum(r, "predicted_target_beat"),
                           fnum(r, "actual_start_beat"),
                           fnum(r, "quantum_beats"))
        if None in (pred, actual, q) or q <= 0:
            continue
        p2_total += 1
        snapped = round(actual / q) * q
        if abs(snapped - pred) <= GRID_TOL_BEATS:
            p2_ok += 1
        else:
            p2_bad.append((r["t_wall"], pred, actual, snapped))

    stats = None
    if look:
        stats = {
            "n": len(look), "min": look[0], "max": look[-1],
            "p10": percentile(look, 10), "median": percentile(look, 50),
            "mean": sum(look) / len(look),
        }

    return {
        "path": path, "fired": len(fired), "playing": len(playing),
        "matched": len(matched), "unmatched": len(unmatched),
        "look": look, "stats": stats,
        "p1_ok": p1_ok, "p1_total": p1_total,
        "p2_ok": p2_ok, "p2_total": p2_total, "p2_bad": p2_bad,
    }


def report(r, bin_ms):
    print(f"\n=== {r['path']}")
    print(f"  fired={r['fired']}  playing={r['playing']}  "
          f"matched={r['matched']}  playing-with-no-fired={r['unmatched']}")
    s = r["stats"]
    if s:
        print(f"  lookahead_ms  n={s['n']}  min={s['min']:.1f}  "
              f"p10={s['p10']:.1f}  median={s['median']:.1f}  "
              f"mean={s['mean']:.1f}  max={s['max']:.1f}")
        print(histogram(r["look"], bin_ms))
    else:
        print("  no lookahead numbers (quantization None, or no fired rows)")
    if r["p1_total"]:
        print(f"  P1 fired-before-start: {r['p1_ok']}/{r['p1_total']} "
              f"({100.0 * r['p1_ok'] / r['p1_total']:.1f}%, threshold ≥95%)")
    if r["p2_total"]:
        flag = "" if r["p2_ok"] == r["p2_total"] else "  ← HARD GATE FAILED"
        print(f"  P2 grid match:        {r['p2_ok']}/{r['p2_total']} "
              f"(threshold 100%){flag}")
        for t, pred, actual, snapped in r["p2_bad"]:
            print(f"     MISS {t}: predicted {pred:.3f}, "
                  f"observed {actual:.3f} → grid {snapped:.3f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("csvs", nargs="+", help="daemon.py output CSVs")
    ap.add_argument("--bin-ms", type=int, default=100, help="histogram bin")
    ap.add_argument("--png", default=None,
                    help="write a matplotlib histogram of all lookaheads")
    args = ap.parse_args()

    results = [analyze_file(p) for p in args.csvs]
    for r in results:
        report(r, args.bin_ms)

    pooled = sorted(v for r in results for v in r["look"])
    if len(args.csvs) > 1 and pooled:
        print(f"\n=== pooled ({len(pooled)} launches)")
        print(f"  min={pooled[0]:.1f}  p10={percentile(pooled, 10):.1f}  "
              f"median={percentile(pooled, 50):.1f}  max={pooled[-1]:.1f}")
        print(histogram(pooled, args.bin_ms))

    print("\nP3 (median ≥ 500 ms) and P4 (p10 ≥ 100 ms) apply to the A1 cell "
          "at 1 Bar / 120 BPM — read them off that file's stats line.")

    if args.png:
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
        except ImportError:
            sys.exit("--png needs matplotlib: pip install matplotlib")
        vals = pooled or (results[0]["look"] if results else [])
        plt.figure(figsize=(8, 4))
        plt.hist(vals, bins=range(0, int(max(vals)) + args.bin_ms, args.bin_ms))
        plt.xlabel("lookahead (ms)")
        plt.ylabel("launches")
        plt.title("S1 Phase A — lookahead at scene launch")
        plt.tight_layout()
        plt.savefig(args.png, dpi=150)
        print(f"wrote {args.png}")


if __name__ == "__main__":
    main()
