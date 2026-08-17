// A1 rev2 — PSRAM stall microbenchmark
//
// Measures how badly PSRAM read latency degrades on the observer core while
// the other core is (a) writing flash, or (b) hammering PSRAM.
//
// No network. No audio. No Link. Just the memory bus.
//
// rev2 changes vs rev1:
//   * RESERVOIR SAMPLING. rev1 stored the first 8192 samples and dropped the
//     rest, so any phase that suspended the observer reported statistics for
//     only the batches that completed -- survivorship bias that made the P4
//     FLASH phase look clean when the observer ran 4% of the time. Stored
//     samples are now a uniform random sample of ALL samples.
//   * WALL-CLOCK GAP TRACKING. Measures suspension directly instead of
//     inferring it from a missing sample count. gap_max_us is the headline:
//     how long was this core stopped dead.
//   * rate_hz computed from total samples attempted, not stored samples.
//   * BUF_BYTES fixed at 4 MB so S3 and P4 runs are directly comparable.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "sdkconfig.h"

// ---------------------------------------------------------------- tunables

#define BUF_BYTES     (4 * 1024 * 1024)  // same on both targets, >> any cache
#define STRESS_BYTES  (512 * 1024)
#define BATCH_READS   64
#define RESERVOIR     4096               // stored samples per phase per metric
#define PHASE_MS      5000
#define CYCLES        4

#define CPU_MHZ       CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ

// ---------------------------------------------------------------- state

typedef enum {
    PHASE_IDLE = 0,
    PHASE_FLASH,
    PHASE_PSRAM,
    PHASE_COUNT
} phase_t;

static const char *kPhaseName[PHASE_COUNT] = {"IDLE", "FLASH", "PSRAM"};

static volatile phase_t g_phase     = PHASE_IDLE;
static volatile bool    g_run       = true;
static volatile bool    g_stress_on = false;

static uint32_t *g_buf;
static uint32_t *g_stress_src;
static uint32_t *g_stress_dst;

// Reservoirs and counters live in internal RAM so recording can't itself stall.
static uint32_t g_lat[PHASE_COUNT][RESERVOIR];   // batch duration, CPU cycles
static uint32_t g_gap[PHASE_COUNT][RESERVOIR];   // inter-sample gap, us
static uint32_t g_total[PHASE_COUNT];            // every sample attempted
static uint32_t g_lat_max[PHASE_COUNT];
static uint32_t g_gap_max[PHASE_COUNT];

static volatile uint32_t g_sink;

// ---------------------------------------------------------------- observer

static uint32_t IRAM_ATTR measure_batch(uint32_t *rng)
{
    uint32_t x = *rng;
    const uint32_t mask = (BUF_BYTES / sizeof(uint32_t)) - 1;

    const uint32_t t0 = esp_cpu_get_cycle_count();
    uint32_t acc = 0;
    for (int i = 0; i < BATCH_READS; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        acc += g_buf[x & mask];
    }
    const uint32_t t1 = esp_cpu_get_cycle_count();

    g_sink += acc;
    *rng = x;
    return t1 - t0;
}

// Vitter reservoir R: once full, sample n replaces a uniformly chosen slot
// with probability RESERVOIR/n. The result is a uniform random sample of the
// whole stream, so percentiles stay unbiased even when far more samples
// arrive than we can store.
static inline void reservoir_put(uint32_t *res, uint32_t n, uint32_t v,
                                 uint32_t *rng)
{
    if (n < RESERVOIR) {
        res[n] = v;
        return;
    }
    uint32_t x = *rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *rng = x;
    const uint32_t j = x % (n + 1);
    if (j < RESERVOIR) res[j] = v;
}

static void observer_task(void *arg)
{
    (void)arg;
    uint32_t rng = 0x12345678u;
    uint32_t rng_r = 0x9E3779B9u;

    for (size_t i = 0; i < BUF_BYTES / sizeof(uint32_t); i += 1024) {
        g_sink += g_buf[i];
    }

    int64_t last_us = esp_timer_get_time();

    while (g_run) {
        const phase_t p = g_phase;

        const int64_t now_us = esp_timer_get_time();
        const int64_t gap = now_us - last_us;
        last_us = now_us;

        const uint32_t cyc = measure_batch(&rng);

        if (p < PHASE_COUNT) {
            const uint32_t n = g_total[p];
            reservoir_put(g_lat[p], n, cyc, &rng_r);
            reservoir_put(g_gap[p], n, (uint32_t)(gap > 0 ? gap : 0), &rng_r);
            if (cyc > g_lat_max[p]) g_lat_max[p] = cyc;
            if (gap > (int64_t)g_gap_max[p]) g_gap_max[p] = (uint32_t)gap;
            g_total[p] = n + 1;
        }

        // One sample per tick (1 kHz at FREERTOS_HZ=1000): paces samples
        // evenly across the phase and keeps the idle task alive.
        vTaskDelay(1);
    }
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------- stressors

static void flash_stress_task(void *arg)
{
    const esp_partition_t *part = (const esp_partition_t *)arg;
    static uint8_t pattern[4096];
    memset(pattern, 0xA5, sizeof(pattern));

    size_t off = 0;
    while (g_run) {
        if (g_phase == PHASE_FLASH && g_stress_on) {
            if (off + sizeof(pattern) > part->size) off = 0;
            esp_partition_erase_range(part, off, 4096);
            esp_partition_write(part, off, pattern, sizeof(pattern));
            off += 4096;
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    vTaskDelete(NULL);
}

static void psram_stress_task(void *arg)
{
    (void)arg;
    while (g_run) {
        if (g_phase == PHASE_PSRAM && g_stress_on) {
            memcpy(g_stress_dst, g_stress_src, STRESS_BYTES);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------- stats

static int cmp_u32(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a;
    const uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static double cyc_to_ns_per_read(double cycles)
{
    return (cycles * 1000.0) / ((double)CPU_MHZ * (double)BATCH_READS);
}

// ---------------------------------------------------------------- main

void app_main(void)
{
#if defined(CONFIG_IDF_TARGET)
    const char *target = CONFIG_IDF_TARGET;
#else
    const char *target = "unknown";
#endif

    printf("\n=== A1 rev2 PSRAM stall benchmark ===\n");
    printf("target=%s cpu=%d MHz batch=%d buf=%d MB reservoir=%d\n",
           target, CPU_MHZ, BATCH_READS, BUF_BYTES / (1024 * 1024), RESERVOIR);

    g_buf        = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_SPIRAM);
    g_stress_src = heap_caps_malloc(STRESS_BYTES, MALLOC_CAP_SPIRAM);
    g_stress_dst = heap_caps_malloc(STRESS_BYTES, MALLOC_CAP_SPIRAM);
    if (!g_buf || !g_stress_src || !g_stress_dst) {
        printf("FATAL: PSRAM alloc failed. free SPIRAM = %d bytes\n",
               (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return;
    }
    memset(g_stress_src, 0x5A, STRESS_BYTES);

    const esp_partition_t *scratch = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "scratch");
    if (!scratch) {
        printf("FATAL: no 'scratch' partition. Check partitions.csv.\n");
        return;
    }
    printf("scratch partition: %d KB\n", (int)(scratch->size / 1024));

    xTaskCreatePinnedToCore(observer_task, "observer", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(flash_stress_task, "flash", 4096,
                            (void *)scratch, 5, NULL, 0);
    xTaskCreatePinnedToCore(psram_stress_task, "psramst", 4096, NULL, 5, NULL, 0);

    vTaskDelay(pdMS_TO_TICKS(500));

    for (int c = 0; c < CYCLES; ++c) {
        for (int p = 0; p < PHASE_COUNT; ++p) {
            g_phase = (phase_t)p;
            g_stress_on = (p != PHASE_IDLE);
            vTaskDelay(pdMS_TO_TICKS(PHASE_MS));
        }
        printf("cycle %d/%d done\n", c + 1, CYCLES);
    }

    g_stress_on = false;
    g_phase = PHASE_IDLE;
    vTaskDelay(pdMS_TO_TICKS(200));
    g_run = false;
    vTaskDelay(pdMS_TO_TICKS(200));

    const double phase_s = (double)(PHASE_MS * CYCLES) / 1000.0;

    printf("\n# n_total = every sample attempted. n_stored = reservoir size.\n");
    printf("# Latency percentiles are ns per read, from the reservoir.\n");
    printf("# gap_* is wall-clock between consecutive samples. Expected\n");
    printf("# ~1000 us (the vTaskDelay). Anything larger is suspension.\n");
    printf("RESULT,target,phase,n_total,n_stored,rate_hz,");
    printf("p50_ns,p99_ns,max_ns,gap_p99_us,gap_max_us\n");

    double p99_ref = 0.0, gapmax_ref = 0.0;

    for (int p = 0; p < PHASE_COUNT; ++p) {
        const uint32_t n = g_total[p];
        const uint32_t stored = (n < RESERVOIR) ? n : RESERVOIR;
        if (stored == 0) {
            printf("RESULT,%s,%s,0,0,0,0,0,0,0,0\n", target, kPhaseName[p]);
            continue;
        }

        qsort(g_lat[p], stored, sizeof(uint32_t), cmp_u32);
        qsort(g_gap[p], stored, sizeof(uint32_t), cmp_u32);

        const double p50 = cyc_to_ns_per_read(g_lat[p][(stored * 50) / 100]);
        const double p99 = cyc_to_ns_per_read(g_lat[p][(stored * 99) / 100]);
        const double mx  = cyc_to_ns_per_read(g_lat_max[p]);
        const double g99 = (double)g_gap[p][(stored * 99) / 100];
        const double gmx = (double)g_gap_max[p];

        if (p == PHASE_IDLE) { p99_ref = p99; gapmax_ref = gmx; }

        printf("RESULT,%s,%s,%u,%u,%.1f,%.1f,%.1f,%.1f,%.0f,%.0f\n",
               target, kPhaseName[p], (unsigned)n, (unsigned)stored,
               (double)n / phase_s, p50, p99, mx, g99, gmx);
    }

    printf("\n# RATIO is clock-independent -- compare these across chips.\n");
    printf("# stall_max_us is the direct measure: how long the core stopped.\n");
    for (int p = 1; p < PHASE_COUNT; ++p) {
        const uint32_t stored =
            (g_total[p] < RESERVOIR) ? g_total[p] : RESERVOIR;
        if (stored == 0) continue;
        qsort(g_lat[p], stored, sizeof(uint32_t), cmp_u32);
        const double p99 = cyc_to_ns_per_read(g_lat[p][(stored * 99) / 100]);
        printf("RATIO,%s,%s,p99=%.2fx,stall_max_us=%.0f,stall_vs_idle=%.1fx\n",
               target, kPhaseName[p],
               p99_ref > 0 ? p99 / p99_ref : 0.0,
               (double)g_gap_max[p],
               gapmax_ref > 0 ? (double)g_gap_max[p] / gapmax_ref : 0.0);
    }

    printf("\n# SANITY: rate_hz should be ~1000 in EVERY phase. A phase well\n");
    printf("# below that means the observer was suspended -- read gap_max_us.\n");
    printf("done.\n");
}
