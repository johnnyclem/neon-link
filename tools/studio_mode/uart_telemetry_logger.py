#!/usr/bin/env python3
"""Tail the module's console UART and capture one telemetry stream to a CSV.

The firmware prints "TEL,<header-or-data>\\n" lines on the console UART
once cfg.debug.telemetry_uart_csv is on (docs/STUDIO_MODE_TEST_PLAN.md
P4), and "PLL,..." lines for the per-tick MIDI clock PLL stream
(docs/MIDI_PLL_PHASES_HANDOFF.md Phase E) behind the same flag. This
filters one stream (--prefix) out of the ordinary ESP_LOG noise on the
same port and writes it to a plain CSV file: one header row, then data
rows for as long as it runs.

Usage:
    python uart_telemetry_logger.py --port /dev/tty.usbmodemXXXX --out cell_a_run1.csv
    python uart_telemetry_logger.py --port COM5 --baud 115200 --out run.csv
    python uart_telemetry_logger.py --port COM5 --prefix PLL --out pll_ticks.csv
"""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", required=True)
    ap.add_argument(
        "--prefix",
        default="TEL",
        help="line tag to capture: TEL (1 Hz service stream, default) or "
        "PLL (per-tick MIDI clock PLL stream)",
    )
    ap.add_argument(
        "--echo",
        action="store_true",
        help="also echo every non-telemetry line to stderr (debug)",
    )
    args = ap.parse_args()
    tag = args.prefix.rstrip(",") + ","

    ser = serial.Serial(args.port, args.baud, timeout=1)
    rows = 0
    header_written = False
    t0 = time.time()
    with open(args.out, "w", newline="") as f:
        print(f"logging {tag[:-1]} lines from {args.port} to {args.out} (Ctrl-C to stop)")
        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                try:
                    line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                except UnicodeDecodeError:
                    continue
                if not line.startswith(tag):
                    if args.echo:
                        print(line, file=sys.stderr)
                    continue
                payload = line[len(tag) :]
                # Every stream's header row leads with a column name
                # (uptime_ms..., t_us...); data rows lead with a number.
                is_header = payload[:1].isalpha()
                if is_header:
                    if header_written:
                        continue  # re-enabling telemetry reprints it; keep one
                    f.write(payload + "\n")
                    header_written = True
                else:
                    if not header_written:
                        # Started mid-stream (telemetry was already on): a
                        # data row with no header is unusable downstream.
                        print(
                            "! first line was data, not the header -- restart "
                            "telemetry_uart_csv or reconnect",
                            file=sys.stderr,
                        )
                        continue
                    f.write(payload + "\n")
                    rows += 1
                f.flush()
                if rows and rows % 30 == 0:
                    elapsed = time.time() - t0
                    print(f"{rows} rows, {elapsed / 60:.1f} min elapsed")
        except KeyboardInterrupt:
            pass
    print(f"done: {rows} rows written to {args.out}")


if __name__ == "__main__":
    main()
