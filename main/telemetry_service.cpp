#include "sdkconfig.h"
#include "tasks.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/pulse_hw_gptimer.hpp"
#include "neon/fixed_math.hpp"
#include "neon/telemetry/emitter.hpp"
#include "neon/telemetry/linksync_csv.hpp"
#include "neon/transport.hpp"
#include "netman/net_manager.h"
#include "wifi.h"

#include <cstdio>

#if CONFIG_NEON_LINKSYNC

namespace {

void tel_task(void*) {
  neon::TelemetryTicker ticker(/*ticks_per_line=*/4);  // 250 ms × 4 = 1 Hz
  for (;;) {
    const neon::Config& cfg = neon_config();
    const neon::TelemetryTick tick =
        ticker.tick(cfg.telemetry_uart_csv != 0);
    if (tick.want_header) {
      char header[192];
      const size_t n =
          neon::linksync_telemetry_csv_header(header, sizeof(header));
      if (n != 0) {
        printf("TEL,%s\n", header);
      }
    }
    if (tick.want_line) {
      neon::TimelineSnapshot tl{};
      timeline_bus().read(tl);
      const halesp::PulseStats ps = halesp::pulse_stats();
      const neon::ActiveNet net = netman::preference().active();
      const bool ap = netman::ap_is_up();
      const bool sta = net == neon::ActiveNet::kWifi;
      const char* mode = neon::telemetry_mode_str(false, ap, sta);

      neon::LinkSyncTelemetrySample s;
      s.uptime_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
      s.mode = mode;
      s.rssi = neon_wifi_rssi();
      s.heap_free_internal = static_cast<uint32_t>(
          heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
      s.heap_free_psram = static_cast<uint32_t>(
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
      s.peers = tl.num_peers;
      s.playing = tl.playing;
      const uint64_t mpb_us =
          tl.tempo_mpb_q32 != 0 ? ((tl.tempo_mpb_q32 + (1ull << 31)) >> 32)
                                : 0;
      s.milli_bpm =
          mpb_us != 0 ? neon::milli_bpm_from_mpb_us(mpb_us) : 0;
      s.pulse_edges = ps.edges;
      s.late_max_us = ps.late_max_us;
      s.late_avg_us = ps.late_avg_us;
      char line[192];
      const size_t n = neon::linksync_telemetry_csv_line(s, line, sizeof(line));
      if (n != 0) {
        printf("TEL,%s\n", line);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

}  // namespace

void neon_start_telemetry_service() {
  xTaskCreatePinnedToCore(tel_task, "tel", 4096, nullptr, 3, nullptr, 0);
}

#else

void neon_start_telemetry_service() {}

#endif
