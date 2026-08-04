// Core 0: the Ableton Link service task. Brings up WiFi (if configured),
// starts the Link session, and publishes integer timeline snapshots to the
// pulse engine whenever the session state materially changes.

#include "ablink/session.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/clkin_capture.hpp"
#include "halesp/tempo_cv_ledc.hpp"
#include "neon/ext_clock.hpp"
#include "neon/link_snapshot.hpp"
#include "neon/tempo_cv.hpp"

#include "board_pins.h"
#include "app_state/config_store.h"
#include "netman/net_manager.h"
#include "tasks.h"
#include "app_state/timeline_bus.h"
#include "wifi.h"

namespace {

const char* kTag = "link_svc";
constexpr TickType_t kCapturePeriod = pdMS_TO_TICKS(10);
constexpr uint32_t kWifiWaitMs = 15000;

void link_service_task(void*) {
  netman::init_common();
  netman::preference().wifi_configured(neon_wifi_has_credentials());
  // Ethernet first: when a cable is present it outranks WiFi (route
  // priority), and Link's interface scanner picks it up automatically.
  netman::ethernet_start();
  if (neon_wifi_has_credentials()) {
    neon_wifi_start();
    if (!neon_wifi_wait_ip(kWifiWaitMs)) {
      ESP_LOGW(kTag, "no IP after %lu ms; starting Link anyway (local session)",
               static_cast<unsigned long>(kWifiWaitMs));
    }
  }
  netman::mdns_start();

  auto& session = ablink::session();
  session.start(120.0);
  ESP_LOGI(kTag, "Link session started");

  if (!halesp::tempo_cv_init(kPinTempoCv)) {
    ESP_LOGW(kTag, "tempo CV init failed");
  }

  neon::ExtClockEstimator ext_clock;
  ext_clock.set_input_ppqn(neon_config().clock_in_ppqn);
  if (!halesp::clkin_capture_init(kPinClkIn, kPinRstIn)) {
    ESP_LOGW(kTag, "CLK/RST IN capture init failed");
  }
  bool ext_active = false;

  neon::TimelineSnapshot prev{};
  bool have_prev = false;
  uint32_t last_logged_peers = UINT32_MAX;
  double last_logged_tempo = 0.0;
  neon::ActiveNet last_net = neon::ActiveNet::kNone;
  bool ap_recommended_logged = false;

  for (;;) {
    // Bidirectional path: drain CLK/RST IN edges, follow the external
    // clock when configuration allows (SOFTWARE.md §5 "External Clock
    // Master mode").
    halesp::CaptureEvent ev;
    while (halesp::clkin_capture_pop(&ev)) {
      if (ev.kind == halesp::CaptureKind::kClock) {
        ext_clock.on_pulse(ev.t_us);
      } else {
        ext_clock.on_reset(ev.t_us);
      }
    }
    ext_clock.set_input_ppqn(neon_config().clock_in_ppqn);
    const neon::ClockSource source = neon_config().clock_source;
    const bool follow_external =
        source != neon::ClockSource::kLinkMaster &&
        ext_clock.active(esp_timer_get_time());
    if (follow_external != ext_active) {
      ESP_LOGI(kTag, "external clock %s",
               follow_external ? "active: following CLK IN" : "lost");
      ext_active = follow_external;
      app_status_set_ext_clock(follow_external);
    }
    if (follow_external) {
      uint32_t mbpm = 0;
      if (ext_clock.take_tempo_update(&mbpm)) {
        ESP_LOGI(kTag, "external tempo -> %u.%03u BPM",
                 static_cast<unsigned>(mbpm / 1000),
                 static_cast<unsigned>(mbpm % 1000));
        session.set_tempo(static_cast<double>(mbpm) / 1000.0);
      }
      int64_t downbeat_us = 0;
      if (ext_clock.take_phase_request(&downbeat_us)) {
        ESP_LOGI(kTag, "RST IN: anchoring downbeat");
        session.request_beat_at_time(downbeat_us);
      }
    } else {
      // Consume stale one-shots so they don't fire on reactivation.
      uint32_t scratch_t = 0;
      int64_t scratch_p = 0;
      ext_clock.take_tempo_update(&scratch_t);
      ext_clock.take_phase_request(&scratch_p);
    }

    const neon::ActiveNet net = netman::preference().active();
    if (net != last_net) {
      ESP_LOGI(kTag, "active network: %s",
               net == neon::ActiveNet::kEthernet ? "ethernet"
               : net == neon::ActiveNet::kWifi   ? "wifi"
                                                 : "none");
      last_net = net;
      ap_recommended_logged = false;
    }
    if (netman::preference().update_should_start_ap(esp_timer_get_time()) &&
        !ap_recommended_logged) {
      ESP_LOGW(kTag, "no connectivity: setup AP would start here "
                     "(AP flow arrives with the web editor milestone)");
      ap_recommended_logged = true;
    }
    hal::LinkState state;
    if (session.capture(state)) {
      neon::TimelineSnapshot snap;
      if (neon::build_snapshot(state, have_prev ? &prev : nullptr, snap)) {
        timeline_bus().publish(snap);
        prev = snap;
        have_prev = true;
      }
      if (state.num_peers != last_logged_peers ||
          state.tempo_bpm != last_logged_tempo) {
        ESP_LOGI(kTag, "peers=%u tempo=%.2f playing=%d",
                 static_cast<unsigned>(state.num_peers), state.tempo_bpm,
                 state.playing ? 1 : 0);
        const auto& cfg = neon_config();
        halesp::tempo_cv_set_ratio(neon::tempo_cv_ratio_q16(
            static_cast<uint32_t>(state.tempo_bpm * 1000.0),
            cfg.tempo_cv_min_bpm, cfg.tempo_cv_max_bpm));
        last_logged_peers = state.num_peers;
        last_logged_tempo = state.tempo_bpm;
      }
      app_status_set_peers(state.num_peers);
    }
    neon_config_flush(esp_timer_get_time());
    vTaskDelay(kCapturePeriod);
  }
}

}  // namespace

void neon_start_link_service() {
  xTaskCreatePinnedToCore(link_service_task, "link_svc", 8192, nullptr, 10,
                          nullptr, 0);
}
