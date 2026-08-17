# A1 rev2 — RUNBOOK

**Read this whole file before running anything.**

This is not a pass/fail test. It has no pass condition. It prints latency
distributions and a human interprets them. If you report "passed", you have
misunderstood the task.

**Goal:** re-run A1 on BOTH boards with rev2 and produce two blocks of CSV
output. That is the entire deliverable.

---

## Why we are re-running

The rev1 P4 run produced `FLASH,n=820` against ~20000 expected. The observer
task was scheduled about 4% of that phase. The statistics it printed came only
from batches that finished, so the phase looked clean when the core was
actually being suspended. That is survivorship bias, and it inverted the
conclusion.

rev2 fixes two things:

1. **Reservoir sampling** — stored samples are now a uniform random sample of
   all samples, so percentiles stay correct no matter how many arrive.
2. **Wall-clock gap tracking** — `gap_max_us` measures suspension directly
   instead of inferring it from a missing sample count.

Also: the rev1 P4 run used **HEX PSRAM at 20 MHz**, giving an idle read
latency of 1823 ns — 3.5x slower than the S3's octal 80 MHz. The P4 supports
far more than 20 MHz. **That configuration is wrong and must be fixed before
the numbers mean anything.** See step 3.

---

## Step 1 — files

Replace `main/psram_stall.c` with the rev2 version. Everything else
(`partitions.csv`, `CMakeLists.txt`, the sdkconfig defaults) is unchanged.

`main/psram_stall_rev1.c.bak` is the old version. Do not build it. Do not
merge it. If it is present in the build, delete it — two `app_main`
definitions will fail to link.

---

## Step 2 — S3 run (do this one first, it needs no config changes)

Plug in the AMYboard. Then:

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

**It takes about 65 seconds of runtime.** You must see `cycle 1/4` through
`cycle 4/4` scroll past, then the RESULT block, then `done.`

If it finishes in under 60 seconds, it did not run — look for a line starting
`FATAL:` and report that instead.

Capture everything from `=== A1 rev2` to `done.` inclusive.

---

## Step 3 — P4 PSRAM configuration (THE CRITICAL STEP)

Do not skip this. The rev1 P4 run is invalid because of it.

```bash
idf.py fullclean
idf.py set-target esp32p4
idf.py menuconfig
```

Navigate to: **Component config → ESP PSRAM → SPI RAM config**

Two settings to change:

- **Mode**: whatever the Waveshare ESP32-P4-Module documentation specifies for
  this board. Do not guess.
- **Speed**: select the **highest option offered**. It was running at 20 MHz,
  which is almost certainly the conservative fallback rather than a deliberate
  choice.

Save and exit.

**Then report, in your output, the exact values you selected**, copied from
`sdkconfig`:

```bash
grep -E "CONFIG_SPIRAM_(MODE|SPEED)" sdkconfig
grep "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=" sdkconfig
```

Paste those three lines with the results. Without them the numbers cannot be
interpreted.

---

## Step 4 — P4 run

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

Same expectations as step 2: `cycle 1/4` … `cycle 4/4`, then results.

---

## Step 5 — validation gates

**Check these before reporting. If any gate fails, say so explicitly rather
than presenting the numbers as final.**

| Gate | Requirement | If it fails |
|---|---|---|
| **G1** | `rate_hz` for IDLE is 950–1050 on both boards | Pacing is broken; results invalid |
| **G2** | P4 `IDLE p50_ns` is **under 900** | PSRAM still misconfigured — go back to step 3, do not report |
| **G3** | S3 `IDLE p50_ns` is 450–600 | Should match the rev1 run (519.5); if not, something else changed |
| **G4** | `n_stored` is 4096 for every phase with `n_total` ≥ 4096 | Reservoir not filling |
| **G5** | Every phase printed a non-zero `n_total` | A phase never ran |

G2 is the one that invalidated the last run. The P4 at proper PSRAM speed
should be **faster** than the S3's 519 ns, not 3.5x slower.

---

## Step 6 — what to report

Paste, verbatim, with no summarising and no interpretation:

1. The three `grep` lines from step 3 (P4 config)
2. The full S3 output block
3. The full P4 output block
4. Which validation gates passed and which failed

Do not compute ratios yourself. Do not draw conclusions. Do not describe the
result as passing or failing. The CSV is the deliverable.

---

## Do not

- Do not report success based on a clean build. Building is not running.
- Do not run this without a board attached to a serial port.
- Do not edit `psram_stall.c` to make a gate pass.
- Do not change `BUF_BYTES`, `BATCH_READS`, `RESERVOIR`, `PHASE_MS` or
  `CYCLES`. They are fixed at these values specifically so the S3 and P4 runs
  are comparable. rev1's S3 run used a 4 MB buffer and the P4 used 8 MB; rev2
  pins both at 4 MB, which is why the S3 must be re-run too rather than
  reusing the rev1 numbers.
- Do not point this at a board with anything you care about in flash. The
  `scratch` partition is erased and rewritten continuously for 20 seconds.

---

## Reference: the rev1 numbers being replaced

```
# S3 (valid, but 4 MB buffer and no gap tracking)
RESULT,esp32s3,IDLE, p50=519.5  p99=528.5   max=963.6
RESULT,esp32s3,FLASH,p50=519.5  p99=83222.6 max=83814.1  n=2803
RESULT,esp32s3,PSRAM,p50=1260.4 p99=1303.1  max=1342.2

# P4 (INVALID: 20 MHz PSRAM, survivorship-biased FLASH row)
RESULT,esp32p4,IDLE, p50=1822.7 p99=2176.2  max=3585.3
RESULT,esp32p4,FLASH,p50=1823.2 p99=1927.3  max=1998.5   n=820  <-- 4% scheduled
RESULT,esp32p4,PSRAM,p50=4397.8 p99=4922.9  max=5345.8
```

The open question rev2 answers: **does the P4's flash controller suspend the
other core the way the S3's does?** The S3 measured a 5.4 ms stall. If the P4
does the same, flash behaviour stops differentiating the two chips and the
port rests entirely on the P4's 768 KB of internal SRAM.
