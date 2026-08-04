// Core 0: BLE MIDI + TRS MIDI service. Receives BLE-MIDI packets from the
// NimBLE host task through a queue, parses and routes them (notes ->
// gate/pitch CV, CCs -> parameters, transport -> Link), and generates the
// Link-derived 24 PPQN MIDI clock on the TRS output via a re-arming
// esp_timer so clock bytes land on the session grid (~100 µs timer
// accuracy — far inside MIDI-clock tolerance).

#include <cstring>

#include "ablink/session.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "blemidi/ble_midi.h"
#include "board_pins.h"
#include "halesp/midi_uart.hpp"
#include "halesp/tempo_cv_ledc.hpp"
#include "neon/midi/ble_midi_parser.hpp"
#include "neon/midi/midi_encoder.hpp"
#include "neon/midi/router.hpp"
#include "neon/multi_engine.hpp"
#include "tasks.h"

namespace {

const char* kTag = "midi_svc";

struct Packet {
  uint8_t len;
  uint8_t data[64];
};

QueueHandle_t g_packets = nullptr;

void on_ble_packet(const uint8_t* data, size_t len) {
  Packet p;
  p.len = static_cast<uint8_t>(len > sizeof(p.data) ? sizeof(p.data) : len);
  std::memcpy(p.data, data, p.len);
  xQueueSend(g_packets, &p, 0);  // drop on overflow; never block NimBLE
}

// --- Router sink: turn routing decisions into module actions ------------

class Sink final : public neon::IRouterSink {
 public:
  void gate(uint8_t target, bool on) override {
    GateEvent ev;
    ev.channel = target == neon::MidiRouteConfig::kTargetRun
                     ? static_cast<uint8_t>(neon::kChRun)
                     : target;
    ev.on = on;
    gate_queue_push(ev);
  }
  void pitch_cv(uint16_t ratio_q16) override {
    halesp::tempo_cv_set_ratio(ratio_q16);
  }
  void latency_offset(int32_t latency_us) override {
    neon::Config cfg = neon_config();
    cfg.engine.latency_us = latency_us;
    neon_config_apply(cfg);
  }
  void shuffle(uint8_t clock_index, uint8_t pct) override {
    neon::Config cfg = neon_config();
    cfg.engine.clocks[clock_index & 3].shuffle_pct = pct;
    neon_config_apply(cfg);
  }
  void transport(bool play) override { ablink::session().set_playing(play); }
  void trs_realtime(uint8_t status) override {
    halesp::midi_uart_send_byte(status);
  }
};

Sink g_sink;
neon::MidiRouter g_router({}, &g_sink);
neon::BleMidiParser g_parser(&g_router);

// --- Link-derived MIDI clock on TRS -------------------------------------

esp_timer_handle_t g_clock_timer = nullptr;

bool link_clock_enabled() {
  const neon::Config& cfg = neon_config();
  return cfg.midi_clock_out != 0 &&
         cfg.midi.clock_policy !=
             neon::MidiRouteConfig::ClockPolicy::kReplace;
}

// Computes the next 24 PPQN tick strictly after now; returns false when no
// timeline exists yet.
bool next_clock_tick_us(int64_t now_us, int64_t* out) {
  neon::TimelineSnapshot tl;
  timeline_bus().read(tl);
  if (tl.tempo_mpb_q32 == 0) {
    return false;
  }
  const double mpb = static_cast<double>(tl.tempo_mpb_q32) / 4294967296.0;
  const double beat =
      static_cast<double>(tl.beat_at_origin_q32) / 4294967296.0 +
      static_cast<double>(now_us - tl.origin_us) / mpb;
  const double tick_beats = 1.0 / 24.0;
  const int64_t tick = static_cast<int64_t>(beat / tick_beats) + 1;
  const double target_beat = static_cast<double>(tick) * tick_beats;
  const double b0 = static_cast<double>(tl.beat_at_origin_q32) / 4294967296.0;
  *out = tl.origin_us +
         static_cast<int64_t>((target_beat - b0) * mpb);
  if (*out <= now_us) {
    *out = now_us + 1000;
  }
  return true;
}

void clock_cb(void*) {
  const int64_t now = esp_timer_get_time();
  if (link_clock_enabled()) {
    halesp::midi_uart_send_byte(neon::midi::kClock);
  }
  int64_t next = 0;
  if (next_clock_tick_us(now, &next)) {
    esp_timer_start_once(g_clock_timer, next - now);
  } else {
    esp_timer_start_once(g_clock_timer, 100000);
  }
}

// --- Service task -------------------------------------------------------

void midi_task(void*) {
  halesp::midi_uart_init(kPinMidiTx);

  bool ble_running = false;
  bool last_playing = false;

  const esp_timer_create_args_t targs = {
      .callback = &clock_cb,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "midi_clk",
      .skip_unhandled_events = true,
  };
  esp_timer_create(&targs, &g_clock_timer);
  esp_timer_start_once(g_clock_timer, 100000);

  for (;;) {
    // BLE kill switch transitions.
    const bool want_ble = neon_config().ble_enabled != 0;
    if (want_ble && !ble_running) {
      ble_running = blemidi::start(&on_ble_packet);
      if (!ble_running) {
        ESP_LOGW(kTag, "BLE MIDI unavailable");
      }
    } else if (!want_ble && ble_running) {
      blemidi::stop();
      ble_running = false;
    }

    g_router.set_config(neon_config().midi);

    Packet p;
    while (xQueueReceive(g_packets, &p, pdMS_TO_TICKS(20)) == pdTRUE) {
      g_parser.feed_packet(p.data, p.len);
    }

    // TRS transport bytes follow the session when Link owns the stream.
    neon::TimelineSnapshot tl;
    timeline_bus().read(tl);
    const bool playing = tl.playing != 0;
    if (playing != last_playing) {
      if (link_clock_enabled()) {
        halesp::midi_uart_send_byte(playing ? neon::midi::kStart
                                            : neon::midi::kStop);
      }
      last_playing = playing;
    }
  }
}

}  // namespace

void neon_start_midi_service() {
  g_packets = xQueueCreate(16, sizeof(Packet));
  xTaskCreatePinnedToCore(midi_task, "midi_svc", 6144, nullptr, 8, nullptr,
                          0);
}
