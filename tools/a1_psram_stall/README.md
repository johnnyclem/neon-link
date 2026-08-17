# A1 — PSRAM stall benchmark

Answers one question: **does the P4 escape the flash-vs-PSRAM contention that
makes the S3 glitch audio when NVS is written?**

No network, no audio, no Link. Just the memory bus.

## Run it

```bash
# S3 (AMYboard)
idf.py set-target esp32s3 && idf.py build flash monitor

# P4 (Waveshare module)
idf.py fullclean && idf.py set-target esp32p4 && idf.py build flash monitor
```

Takes about a minute. Copy the `RESULT,` and `RATIO,` lines from both.

If it aborts on PSRAM allocation, check `idf.py menuconfig` →
Component config → ESP PSRAM. The S3 defaults here (octal, 80 MHz) are set in
`sdkconfig.defaults.esp32s3`; **the P4's PSRAM mode and speed symbols differ
and I haven't verified them** — set them from the Waveshare board docs and
drop them into `sdkconfig.defaults.esp32p4`. Same for the CPU frequency, which
the ns conversion depends on.

## What it does

- **Observer** on core 1: random-stride reads out of an 8 MB PSRAM buffer,
  timed with the CPU cycle counter, one 64-read batch per millisecond. 8 MB
  is far beyond any cache, so every read is a miss.
- **Core 0** cycles through three 5-second phases, four times:
  - `IDLE` — nothing else running (baseline)
  - `FLASH` — erase + write a scratch partition, continuously
  - `PSRAM` — stream 1 MB memcpy through PSRAM, continuously

The measurement loop is `IRAM_ATTR` on purpose. On the S3 a flash write
disables the cache, so code running *from flash* halts too — keeping the loop
in IRAM means you measure the data stall rather than an instruction-fetch
stall.

## Reading the output

```
RESULT,target,phase,n,dropped,p50_ns,p99_ns,max_ns,mean_ns,peak_us
RATIO,target,phase,p99=..x,max=..x
```

**Compare the RATIO lines across chips, not the raw nanoseconds.** The S3 runs
at 240 MHz and the P4 higher, so absolute latency differs for reasons that
have nothing to do with contention. The ratio against that chip's own IDLE
baseline is the clock-independent number.

`peak_us` is the worst single batch as a stall duration. On the S3 expect this
to be large during `FLASH` — IDF halts the other CPU outright during flash
operations, so the observer isn't slowed, it's suspended. That's the
mechanism behind config-save glitches.

## What each outcome means

| | Interpretation |
|---|---|
| S3 FLASH ratio high, P4 low | **H3 confirmed and the P4 fixes it at the source.** Strongest case for the port. |
| Both FLASH ratios high | The P4's remedy is its 768 KB internal SRAM, not better PSRAM behaviour. Port is still valid — but it only pays off if the rings actually land in internal RAM. Force `MALLOC_CAP_INTERNAL` and assert rather than falling back. |
| Both ratios ≈ 1.0 | H3 is small. The P4's justification weakens a lot. Reassess before spending the port. |

The `PSRAM` phase is the second story: pure bus bandwidth contention, which is
what WiFi DMA and lwIP buffers do to your audio rings today given
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`. A high ratio there argues for
getting the rings off PSRAM regardless of which chip wins.

## Caveats

- Untested against a real IDF install — I wrote it without a toolchain here.
  The stats math, percentile indexing, RNG spread and ns conversion are
  verified; the IDF API calls are not.
- `CONFIG_ESP_TASK_WDT_EN=n` because the stressors monopolise core 0.
- The scratch partition gets erased and rewritten continuously. It's 1 MB of
  a 16 MB flash and holds nothing, but don't point this at a board with
  anything you care about on it.
