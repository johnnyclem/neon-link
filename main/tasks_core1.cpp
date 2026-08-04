// Core 1: the real-time pulse engine task. Milestone 3: the full output
// engine — four independently-configured clocks, Reset pulse, Run gate,
// and latency compensation — scheduled through the GPTimer edge emitter
// from the published Link timeline.

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "app_state/config_store.h"
#include "halesp/pulse_hw_gptimer.hpp"
#include "neon/multi_engine.hpp"
#include "tasks.h"
#include "app_state/timeline_bus.h"

namespace {

const char* kTag = "pulse_task";

// Scheduling cadence: refill every 5 ms with a 15 ms horizon and a 2 ms
// minimum lead. The hardware's parked-poll interval is 1 ms, so a freshly
// submitted edge is always noticed with >= 1 ms to spare.
constexpr int64_t kHorizonUs = 15000;
constexpr int64_t kLeadUs = 2000;
constexpr TickType_t kRefillTicks = pdMS_TO_TICKS(5);

// Edge-stream channel index -> GPIO (all < 32; see board_pins.h).
constexpr int kChannelGpio[neon::kChannelCount] = {
    kPinClk1, kPinClk2, kPinClk3, kPinClk4, kPinReset, kPinRun,
};

halesp::PulseHwGptimer g_pulse_hw;

void pulse_task(void*) {
  if (!g_pulse_hw.init(kChannelGpio, neon::kChannelCount)) {
    ESP_LOGE(kTag, "pulse hardware init failed");
    vTaskDelete(nullptr);
    return;
  }

  neon::MultiClockEngine engine;
  engine.set_config(neon_config().engine);

  int64_t cursor = g_pulse_hw.now_us() + kLeadUs;
  uint32_t timeline_version = 0;
  uint32_t config_version = engine_config_bus().version();
  bool have_timeline = false;
  neon::TimelineSnapshot last_snap{};

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    if (timeline_bus().version() != timeline_version) {
      timeline_version = timeline_bus().read(last_snap);
      engine.retime(last_snap, cursor);
      if (!have_timeline) {
        ESP_LOGI(kTag, "timeline acquired; outputs live");
        have_timeline = true;
      }
    }
    if (engine_config_bus().version() != config_version) {
      neon::EngineConfig cfg;
      config_version = engine_config_bus().read(cfg);
      engine.set_config(cfg);
      if (have_timeline) {
        engine.retime(last_snap, cursor);
      }
    }

    const int64_t until = g_pulse_hw.now_us() + kLeadUs + kHorizonUs;
    if (have_timeline && until > cursor) {
      neon::Edge edges[64];
      size_t n;
      do {
        n = engine.generate(cursor, until, edges,
                            sizeof(edges) / sizeof(edges[0]));
        for (size_t i = 0; i < n; ++i) {
          const uint32_t mask = 1u << kChannelGpio[edges[i].channel];
          const hal::PulseEdge pe{edges[i].t_us,
                                  edges[i].high ? mask : 0u,
                                  edges[i].high ? 0u : mask};
          while (!g_pulse_hw.submit(pe)) {
            vTaskDelay(1);
          }
        }
      } while (n == sizeof(edges) / sizeof(edges[0]));
      cursor = until;
    }
    vTaskDelayUntil(&wake, kRefillTicks);
  }
}

}  // namespace

void neon_start_core1_tasks() {
  xTaskCreatePinnedToCore(pulse_task, "pulse", 4096, nullptr,
                          configMAX_PRIORITIES - 2, nullptr, 1);
}
