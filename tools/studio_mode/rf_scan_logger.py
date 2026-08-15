#!/usr/bin/env python3
"""RF characterization for docs/STUDIO_MODE_TEST_PLAN.md §6.

Hits GET /api/scan on the module and logs AP count, channel histogram, and
strongest neighbor RSSI. Run once before and once after every Phase 1-3
run: "a clean result in a quiet environment means something different than
a clean result at 40 visible SSIDs."

Usage:
    python rf_scan_logger.py --host neon-link.local --run B-STA --when before
    python rf_scan_logger.py --host neon-link.local --run B-STA --when after

Appends one JSON line per invocation to rf_scan_log.jsonl (or --out).
"""
import argparse
import json
import sys
import time
from collections import Counter
from pathlib import Path

import requests


def fetch_scan(host, timeout=15.0):
    resp = requests.get(f"http://{host}/api/scan", timeout=timeout)
    resp.raise_for_status()
    return resp.json()


def summarize(entries):
    if not entries:
        return {
            "ap_count": 0,
            "channel_histogram": {},
            "strongest_neighbor_rssi": None,
        }
    channels = Counter(str(e.get("channel", 0)) for e in entries)
    strongest = max(e.get("rssi", -127) for e in entries)
    return {
        "ap_count": len(entries),
        "channel_histogram": dict(
            sorted(channels.items(), key=lambda kv: int(kv[0]))
        ),
        "strongest_neighbor_rssi": strongest,
    }


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--host",
        required=True,
        help="module hostname or IP, e.g. neon-link.local or 192.168.4.1",
    )
    ap.add_argument(
        "--run", required=True, help="run label, e.g. B-STA, Cell-A-1, Phase3-soak"
    )
    ap.add_argument("--when", choices=["before", "after"], required=True)
    ap.add_argument("--out", default="rf_scan_log.jsonl", type=Path)
    args = ap.parse_args()

    try:
        entries = fetch_scan(args.host)
    except requests.RequestException as exc:
        print(f"scan failed: {exc}", file=sys.stderr)
        sys.exit(1)

    record = {
        "ts": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()),
        "run": args.run,
        "when": args.when,
        "host": args.host,
        **summarize(entries),
        "raw": entries,
    }
    with args.out.open("a") as f:
        f.write(json.dumps(record) + "\n")

    print(
        f"[{record['run']}/{record['when']}] {record['ap_count']} APs, "
        f"strongest neighbor {record['strongest_neighbor_rssi']} dBm, "
        f"channels {record['channel_histogram']}"
    )


if __name__ == "__main__":
    main()
