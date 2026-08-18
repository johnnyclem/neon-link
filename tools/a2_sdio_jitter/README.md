# A2 — ESP-Hosted SDIO jitter

P4-only. The S3 has an on-chip radio; this question does not exist there.

A1 closed: the P4 wins the memory bus and still does not escape a flash
stall. The remaining unknown that can **disqualify the chip** is whether
ESP-Hosted SDIO traffic (C6 ↔ P4, CLK 18 / CMD 19 / D0–D3 14–17) injects
the same class of stall into the observer core — continuously, on every
packet, not once per NVS commit.

No audio, no Link, no OLED. Radio + memory bus only.

## Question

Does Hosted SDIO suspend or contend with a core-1 PSRAM observer the way
flash did in A1?

| Outcome | Meaning |
|---|---|
| `gap_max` stays ~1 ms in every radio phase, PSRAM p99 ≲ 2× | SDIO is not A1-class. Port stays open. |
| HOSTED_UP (AP up, no client) already shows 10 ms+ gaps | Heartbeats alone are fatal. Chip is out. |
| Only RPC_HAMMER or TRAFFIC shows 10 ms+ gaps | Maybe survivable if the product never polls Hosted from a realtime path and traffic is shaped. Still a design input, not a pass. |
| Transport never comes up | Not an A2 result. Fix Hosted 1.4 / C6 slave first (`docs/P4DEVKIT.md`). |

Compare `gap_max` to the 42.6 ms DMA ring and to A1 FLASH (51 ms on this
chip). A stall that happens on every SDIO burst is worse than NVS.

First run (P4 v3.1, 200 MHz, Hosted 1.4.7): `results_esp32p4_v31.txt`.
HOSTED `gap_max = 1000 µs`, RPC `1489 µs` / p99 1.30×, TRAFFIC `1093 µs`
with no station. Not A1-class. Does not disqualify. TRAFFIC with a
client flood is still open.

## Phases (5 s × 4, same skeleton as A1 rev2)

1. **IDLE** — WiFi not started. Baseline.
2. **HOSTED_UP** — SoftAP up, no station. SDIO heartbeats only.
3. **RPC_HAMMER** — AP still up; core 0 tight-loops a Hosted RPC
   (`esp_wifi_get_mac`). This is SDIO without airtime.
4. **TRAFFIC** — if a station is associated, UDP echo / flood on the AP
   netif. If no station, the phase is skipped and the CSV says so.

## Config

Same P4 overlays as A1: HEX @ 200 MHz, UART console, `REV_MIN` split
(v1.3 = IDF 5.3.2, v3.1 = IDF 5.5.5). Hosted **1.4** / wifi_remote
**0.14** — do not pull 2.12 against factory C6 `0.0.0`.

## Reading the output

Same columns as A1 rev2. Gates:

| Gate | Requirement |
|---|---|
| G1 | IDLE `rate_hz` 950–1050 |
| G2 | IDLE p50 < 900 ns (else PSRAM is 20 MHz again) |
| G3 | `HOSTED_UP` actually printed a non-zero `n_total` and boot log has `Received INIT event` |
| G4 | `n_stored` = 4096 when `n_total` ≥ 4096 |

Do not report “passed.” Paste the CSV and the three `sdkconfig` greps.
