# A1 — PSRAM stall benchmark — **closed**

Rev2 CSVs (decision-grade):

- `results_esp32s3_rev2.txt` — AMYboard, IDF 5.3.2, octal 80 MHz, 240 MHz
- `results_esp32p4_v31_rev2.txt` — P4 rev v3.1, IDF 5.5.5, HEX 200 MHz, 400 MHz

20 MHz P4 files (`results_esp32p4.txt`, `results_esp32p4_v31.txt`) are
invalid. Do not cite them.

## What it answered

Not “which chip escapes flash-vs-PSRAM.” Neither does. The P4 is faster
and contends less on the data bus; flash still suspends the other core,
and for longer.

| | S3 | P4 @ 200 MHz |
|---|---|---|
| IDLE p50 | 519.5 ns | **449.3 ns** |
| PSRAM contention | 2.46× | **1.80×** |
| Flash stall (`gap_max`) | 22.4 ms | **51.0 ms** |
| Observer scheduled during FLASH | 11% | 4% |

I2S DMA ring is `8 × 256` frames @ 48 kHz = **42.6 ms**. S3 stall is half
the ring (lower bound). P4 stall is the whole ring with margin. One NVS
commit during playback empties the buffer on either board.

Fallout: `docs/FRIENDS_FAMILY_HANDOFF.md` G6 (deferred NVS) and G7
(GPTimer IRAM + refill horizon). P4 v2 board rule: config persistence
must not share the audio SPI controller (`docs/P4DEVKIT.md`).

Next measurement is A2 (`tools/a2_sdio_jitter`) — ESP-Hosted SDIO jitter.
That one can still disqualify the P4.

## How it ran

Observer on core 1: 4 MB PSRAM, 64 random reads per 1 ms tick, Vitter
reservoir, wall-clock `gap_max`. Core 0: IDLE / FLASH / PSRAM, 5 s × 4.
Stressors `vTaskDelay(1)` after each flash op and memcpy so the phase
timer is not starved. HEX @ 200 MHz is mandatory on P4 (G2: IDLE p50 <
900 ns). Methodology: `RUNBOOK.md`.
