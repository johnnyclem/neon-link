# A2 — RUNBOOK

**Read this whole file before flashing anything.**

This is not a pass/fail test. It has no pass condition. It prints latency
distributions and a human interprets them.

**Goal:** one P4 CSV from a Hosted-up run. S3 is not a DUT. That is the
entire deliverable.

## Why this exists

A1 is closed. The P4 is the better memory bus and still loses 51 ms to a
flash erase. The open P4 question is SDIO to the C6: if Hosted injects
that class of stall on the data path, the chip cannot carry Link or
audio. Unlike A1, that result can kill the port.

## Do not

- Do not run this on AMYboard.
- Do not use Hosted 2.12 against factory C6 `0.0.0`.
- Do not mix a v1.3 image (IDF 5.3.2, `REV_MIN_1`) onto a v3.1 board.
- Do not treat a silent UART as a hang — Type-C is USB-UART on these
  kits (GPIO 37/38). WCH adapters flash at 115200.
- Do not report success from a clean build.

## When a board is plugged in

Flash from `tools/a2_sdio_jitter` with the same overlay split as A1
(`sdkconfig.defaults.esp32p4` / `esp32p4v31`). Capture from `=== A2` to
`done.` Paste the `CONFIG_SPIRAM_MODE` / `SPEED` / `CPU_FREQ` greps with
the CSV.

If `HOSTED_UP` never starts, stop and fix the radio. That is not an A2
number.
