// A2 — ESP-Hosted SDIO jitter
//
// P4-only. Same observer as A1 rev2. Stress is the C6 SDIO path, not flash.
// IDLE is measured with WiFi down; radio comes up once, then HOSTED / RPC /
// TRAFFIC run. Do not interleave IDLE after esp_wifi_init().

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

#ifndef CONFIG_IDF_TARGET_ESP32P4
#error "A2 is P4-only (ESP-Hosted SDIO). Use A1 for the S3."
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#define BUF_BYTES    (4 * 1024 * 1024)
#define BATCH_READS  64
#define RESERVOIR    4096
#define PHASE_MS     5000
#define CYCLES       4
#define CPU_MHZ      CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ

typedef enum {
    PHASE_IDLE = 0,
    PHASE_HOSTED,
    PHASE_RPC,
    PHASE_TRAFFIC,
    PHASE_COUNT
} phase_t;

static const char *kPhaseName[PHASE_COUNT] = {
    "IDLE", "HOSTED", "RPC", "TRAFFIC"};

static volatile phase_t g_phase     = PHASE_IDLE;
static volatile bool    g_run       = true;
static volatile bool    g_stress_on = false;
static volatile uint32_t g_sta      = 0;

static uint32_t *g_buf;
static uint32_t g_lat[PHASE_COUNT][RESERVOIR];
static uint32_t g_gap[PHASE_COUNT][RESERVOIR];
static uint32_t g_total[PHASE_COUNT];
static uint32_t g_lat_max[PHASE_COUNT];
static uint32_t g_gap_max[PHASE_COUNT];
static volatile uint32_t g_sink;
static volatile uint32_t g_rpc_ok;
static volatile uint32_t g_tx_ok;

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
        vTaskDelay(1);
    }
    vTaskDelete(NULL);
}

static void rpc_stress_task(void *arg)
{
    (void)arg;
    uint8_t mac[6];
    while (g_run) {
        if (g_phase == PHASE_RPC && g_stress_on) {
            if (esp_wifi_get_mac(WIFI_IF_AP, mac) == ESP_OK) {
                g_rpc_ok++;
                g_sink += mac[5];
            }
            vTaskDelay(1);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    vTaskDelete(NULL);
}

static void traffic_stress_task(void *arg)
{
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd < 0) {
        printf("WARN: UDP socket failed; TRAFFIC will be idle\n");
        while (g_run) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelete(NULL);
        return;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    struct sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(9999);
    dest.sin_addr.s_addr = inet_addr("192.168.4.255");
    uint8_t pkt[256];
    memset(pkt, 0xA5, sizeof(pkt));

    while (g_run) {
        if (g_phase == PHASE_TRAFFIC && g_stress_on) {
            if (sendto(fd, pkt, sizeof(pkt), 0,
                       (struct sockaddr *)&dest, sizeof(dest)) >= 0) {
                g_tx_ok++;
            }
            vTaskDelay(1);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    close(fd);
    vTaskDelete(NULL);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id,
                          void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        g_sta++;
        printf("AP client joined (sta=%u)\n", (unsigned)g_sta);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (g_sta > 0) g_sta--;
        printf("AP client left (sta=%u)\n", (unsigned)g_sta);
    } else if (id == WIFI_EVENT_AP_START) {
        printf("SoftAP started A2-SDIO at 192.168.4.1\n");
    }
}

static bool radio_up(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        printf("FATAL: nvs_flash_init %s\n", esp_err_to_name(err));
        return false;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &on_wifi_event, NULL));

    // esp_wifi_init resets the C6 and waits for SDIO. Do not probe first.
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        printf("FATAL: esp_wifi_init %s (C6/Hosted down?)\n",
               esp_err_to_name(err));
        return false;
    }

    wifi_config_t ap = {};
    memcpy(ap.ap.ssid, "A2-SDIO", 7);
    ap.ap.ssid_len = 7;
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.beacon_interval = 100;

    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) {
        printf("FATAL: SoftAP config failed\n");
        return false;
    }
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        printf("FATAL: esp_wifi_start %s\n", esp_err_to_name(err));
        return false;
    }
    printf("radio up: SoftAP A2-SDIO (open, ch 1) 192.168.4.1\n");
    return true;
}

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

static void run_phase(phase_t p, bool stress)
{
    g_phase = p;
    g_stress_on = stress;
    for (int c = 0; c < CYCLES; ++c) {
        vTaskDelay(pdMS_TO_TICKS(PHASE_MS));
        printf("  %s %d/%d\n", kPhaseName[p], c + 1, CYCLES);
        fflush(stdout);
    }
}

void app_main(void)
{
    printf("\n=== A2 ESP-Hosted SDIO jitter ===\n");
    printf("target=%s cpu=%d MHz psram=%d MHz batch=%d buf=%d MB reservoir=%d\n",
           CONFIG_IDF_TARGET, CPU_MHZ,
#ifdef CONFIG_SPIRAM_SPEED
           CONFIG_SPIRAM_SPEED,
#else
           0,
#endif
           BATCH_READS, BUF_BYTES / (1024 * 1024), RESERVOIR);
    fflush(stdout);

    g_buf = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (!g_buf) {
        printf("FATAL: PSRAM alloc failed. free SPIRAM = %d\n",
               (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return;
    }

    xTaskCreatePinnedToCore(observer_task, "observer", 4096, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    printf("phase IDLE (radio down)\n");
    run_phase(PHASE_IDLE, false);

    printf("bringing up Hosted / C6 …\n");
    fflush(stdout);
    if (!radio_up()) {
        printf("FATAL: radio did not come up. Not an A2 result.\n");
        g_run = false;
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(1500));

    xTaskCreatePinnedToCore(rpc_stress_task, "rpc", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(traffic_stress_task, "tx", 6144, NULL, 5, NULL, 0);

    printf("phase HOSTED (AP up, no hammer)\n");
    run_phase(PHASE_HOSTED, false);

    printf("phase RPC (esp_wifi_get_mac @ 1 kHz)\n");
    run_phase(PHASE_RPC, true);

    printf("phase TRAFFIC (UDP broadcast 192.168.4.255:9999, sta=%u)\n",
           (unsigned)g_sta);
    run_phase(PHASE_TRAFFIC, true);

    g_stress_on = false;
    g_phase = PHASE_IDLE;
    vTaskDelay(pdMS_TO_TICKS(200));
    g_run = false;
    vTaskDelay(pdMS_TO_TICKS(200));

    const double phase_s = (double)(PHASE_MS * CYCLES) / 1000.0;
    printf("\n# rpc_ok=%u tx_ok=%u sta_peak_or_now=%u\n",
           (unsigned)g_rpc_ok, (unsigned)g_tx_ok, (unsigned)g_sta);
    printf("# n_total = every sample attempted. n_stored = reservoir size.\n");
    printf("# gap_* is wall-clock between consecutive samples. Expected ~1000 us.\n");
    printf("RESULT,target,phase,n_total,n_stored,rate_hz,");
    printf("p50_ns,p99_ns,max_ns,gap_p99_us,gap_max_us\n");

    double p99_ref = 0.0, gapmax_ref = 0.0;
    for (int p = 0; p < PHASE_COUNT; ++p) {
        const uint32_t n = g_total[p];
        const uint32_t stored = (n < RESERVOIR) ? n : RESERVOIR;
        if (stored == 0) {
            printf("RESULT,%s,%s,0,0,0,0,0,0,0,0\n",
                   CONFIG_IDF_TARGET, kPhaseName[p]);
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
               CONFIG_IDF_TARGET, kPhaseName[p], (unsigned)n, (unsigned)stored,
               (double)n / phase_s, p50, p99, mx, g99, gmx);
    }

    printf("\n# RATIO vs IDLE. stall_max_us is the direct suspension measure.\n");
    for (int p = 1; p < PHASE_COUNT; ++p) {
        const uint32_t stored =
            (g_total[p] < RESERVOIR) ? g_total[p] : RESERVOIR;
        if (stored == 0) continue;
        qsort(g_lat[p], stored, sizeof(uint32_t), cmp_u32);
        const double p99 = cyc_to_ns_per_read(g_lat[p][(stored * 99) / 100]);
        printf("RATIO,%s,%s,p99=%.2fx,stall_max_us=%.0f,stall_vs_idle=%.1fx\n",
               CONFIG_IDF_TARGET, kPhaseName[p],
               p99_ref > 0 ? p99 / p99_ref : 0.0,
               (double)g_gap_max[p],
               gapmax_ref > 0 ? (double)g_gap_max[p] / gapmax_ref : 0.0);
    }
    printf("\n# SANITY: rate_hz ~1000 in every phase. Well below = suspended.\n");
    printf("done.\n");
    fflush(stdout);
}
