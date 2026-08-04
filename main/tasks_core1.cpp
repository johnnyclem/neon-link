// Core 1: the real-time pulse engine task. Milestone 2: the clock follows
// the Ableton Link session — core 0 publishes timeline snapshots through
// the seqlock bus, this task re-anchors the engine on each new version and
// schedules a 4 PPQN clock on CLK1 through the GPTimer edge emitter.

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "halesp/pulse_hw_gptimer.hpp"
#include "neon/clock_engine.hpp"
#include "tasks.h"
#include "timeline_bus.h"

namespace {

const char* kTag = "pulse_task";

// Scheduling cadence: refill every 5 ms with a 15 ms horizon and a 2 ms
// minimum lead. The hardware's parked-poll interval is 1 ms, so a freshly
// submitted edge is always noticed with >= 1 ms to spare.
constexpr int64_t kHorizonUs = 15000;
constexpr int64_t kLeadUs = 2000;
constexpr TickType_t kRefillTicks = pdMS_TO_TICKS(5);

halesp::PulseHwGptimer g_pulse_hw;

void pulse_task(void*) {
  const int gpios[] = {kPinClk1};
  if (!g_pulse_hw.init(gpios, sizeof(gpios) / sizeof(gpios[0]))) {
    ESP_LOGE(kTag, "pulse hardware init failed");
    vTaskDelete(nullptr);
    return;
  }

  neon::ClockEngine engine;
  neon::OutputSettings out;
  out.ppqn = 4;
  out.trig_len_us = 5000;
  engine.set_output(out);
  // Milestone 2: clocks free-run from the session beat grid; transport
  // gating of outputs arrives with Run/Reset in milestone 3.
  engine.set_transport_gating(false);

  int64_t cursor = g_pulse_hw.now_us() + kLeadUs;
  uint32_t timeline_version = 0;
  bool have_timeline = false;

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    if (timeline_bus().version() != timeline_version) {
      neon::TimelineSnapshot snap;
      timeline_version = timeline_bus().read(snap);
      engine.retime(snap, cursor);
      if (!have_timeline) {
        ESP_LOGI(kTag, "timeline acquired; clock on GPIO%d", kPinClk1);
        have_timeline = true;
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
          const uint32_t mask = 1u << kPinClk1;
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
