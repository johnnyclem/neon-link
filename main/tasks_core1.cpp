// Core 1: the real-time pulse engine task. Milestone 3: the full output
// engine — four independently-configured clocks, Reset pulse, Run gate,
// and latency compensation — scheduled through the GPTimer edge emitter
// from the published Link timeline.
//
// On AMYboard (kPulseVirtual) a second core-1 task mirrors the CLK1 bit
// of the atomic level word onto GP8413 CV out 2 as a 0/5 V gate.

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "board_pins.h"
#include "app_state/config_store.h"
#include "halesp/midi_uart.hpp"
#include "halesp/pulse_hw_gptimer.hpp"
#include "neon/midi/clock_engine.hpp"
#include "neon/multi_engine.hpp"
#include "tasks.h"
#include "app_state/timeline_bus.h"

#if CONFIG_NEON_BOARD_AMYBOARD
#include "halesp/gp8413.hpp"
#endif

namespace {

const char* kTag = "pulse_task";

// Scheduling cadence: refill every 5 ms with a 67 ms horizon and a 2 ms
// minimum lead. 67 ms is 3× the A1 S3 flash stall (22.4 ms is one
// erase-write; a real NVS commit is 2–3× that). G6 is what keeps flash
// off this path; the horizon is what makes a missed commit survivable.
// The ISR parks at 1 ms, so a freshly submitted edge is noticed with
// >= 1 ms to spare.
constexpr int64_t kHorizonUs = 67000;
constexpr int64_t kLeadUs = 2000;
constexpr TickType_t kRefillTicks = pdMS_TO_TICKS(5);

// Edge-stream channel index -> GPIO (all < 32; see board_pins.h).
// On AMYboard these are virtual bit indices, not real pins.
constexpr int kChannelGpio[neon::kChannelCount] = {
    kPinClk1, kPinClk2, kPinClk3, kPinClk4, kPinReset, kPinRun,
};

halesp::PulseHwGptimer g_pulse_hw;

#if CONFIG_NEON_BOARD_AMYBOARD
// Mirror the CLK1 level onto CV jack 2. Polled at 250 µs — I2C write is
// ~100 µs, so this is the practical floor for gate edges on the DAC path.
// Tempo CV is driven separately from the Link service.
void cv_mirror_task(void*) {
  bool last_high = false;
  bool have_last = false;
  for (;;) {
    const uint32_t levels = halesp::PulseHwGptimer::levels();
    const bool high = (levels & (1u << kPinClk1)) != 0;
    if (!have_last || high != last_high) {
      halesp::gp8413_set_volts(kAmyCvClockChannel,
                               high ? kAmyGateHighVolts : kAmyGateLowVolts);
      last_high = high;
      have_last = true;
    }
    // 250 µs busy-wait via delay; FreeRTOS tick is 1 ms so this is 1 tick.
    vTaskDelay(1);
  }
}
#endif

#if CONFIG_NEON_LINKSYNC
bool submit_midi(const neon::midi::Event& ev) {
  uint8_t buf[3];
  const size_t len = neon::midi::encode_event(ev, buf);
  if (len == 0) {
    return true;
  }
  hal::PulseEdge pe{};
  pe.t_us = ev.t_us;
  pe.midi_len = static_cast<uint8_t>(len);
  for (size_t i = 0; i < len; ++i) {
    pe.midi[i] = buf[i];
  }
  while (!g_pulse_hw.submit(pe)) {
    vTaskDelay(1);
  }
  return true;
}
#endif

void pulse_task(void*) {
  if (!g_pulse_hw.init(kChannelGpio, neon::kChannelCount, kPulseVirtual)) {
    ESP_LOGE(kTag, "pulse hardware init failed");
    vTaskDelete(nullptr);
    return;
  }

#if CONFIG_NEON_LINKSYNC
  if ((kPinMidiTx >= 0 || kPinMidiRx >= 0) &&
      !halesp::midi_uart_init(kPinMidiTx, kPinMidiRx)) {
    ESP_LOGE(kTag, "MIDI UART init failed TX=GPIO%d RX=GPIO%d", kPinMidiTx,
             kPinMidiRx);
  }

  neon::midi::ClockEngine midi;
  int64_t cursor = g_pulse_hw.now_us() + kLeadUs;
  uint32_t timeline_version = 0;
  bool have_timeline = false;
  neon::TimelineSnapshot last_snap{};

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    if (timeline_bus().version() != timeline_version) {
      timeline_version = timeline_bus().read(last_snap);
      midi.set_nudge(neon_config().midi_nudge_us);
      midi.retime(last_snap, cursor);
      if (!have_timeline) {
        ESP_LOGI(kTag, "timeline acquired; MIDI clock live");
        have_timeline = true;
      }
    }

    const int64_t until = g_pulse_hw.now_us() + kLeadUs + kHorizonUs;
    if (have_timeline && until > cursor && neon_config().midi_clock_out != 0) {
      neon::midi::Event evs[32];
      size_t n;
      do {
        n = midi.generate(cursor, until, evs, 32);
        for (size_t i = 0; i < n; ++i) {
          submit_midi(evs[i]);
        }
      } while (n == 32);
      cursor = until;
    }
    vTaskDelayUntil(&wake, kRefillTicks);
  }
#else
  neon::MultiClockEngine engine;
  neon::EngineConfig eng_cfg = neon_config().engine;
#if CONFIG_NEON_BOARD_AMYBOARD
  // Only one physical clock jack: keep CLK1 (16ths) enabled, mute the
  // other three digital outs so the engine isn't doing useless work.
  // Users can re-enable via the web editor if they want MIDI-only rates.
  eng_cfg.clocks[1].enabled = false;
  eng_cfg.clocks[2].enabled = false;
  eng_cfg.clocks[3].enabled = false;
#endif
  engine.set_config(eng_cfg);

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

    // MIDI note gates: emit immediately (with the scheduling lead) on the
    // target channel. Point gates at a disabled clock output so the two
    // sources don't fight.
    GateEvent gate;
    while (gate_queue_pop(&gate)) {
      if (gate.channel < neon::kChannelCount) {
        const uint32_t mask = 1u << kChannelGpio[gate.channel];
        const hal::PulseEdge pe{g_pulse_hw.now_us() + kLeadUs,
                                gate.on ? mask : 0u, gate.on ? 0u : mask};
        while (!g_pulse_hw.submit(pe)) {
          vTaskDelay(1);
        }
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
#endif
}

}  // namespace

void neon_start_core1_tasks() {
  // MultiClockEngine + edge buffers + C++ frames need more than 4 KB;
  // AMYboard bring-up saw "stack overflow in task pulse" at 4096.
  xTaskCreatePinnedToCore(pulse_task, "pulse", 8192, nullptr,
                          configMAX_PRIORITIES - 2, nullptr, kNeonCoreRt);
#if CONFIG_NEON_BOARD_AMYBOARD
  xTaskCreatePinnedToCore(cv_mirror_task, "cv_mirror", 4096, nullptr,
                          configMAX_PRIORITIES - 3, nullptr, kNeonCoreRt);
#endif
}
