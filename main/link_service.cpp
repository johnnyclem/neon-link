// Core 0: the Ableton Link service task. Brings up WiFi (if configured),
// starts the Link session, and publishes integer timeline snapshots to the
// pulse engine whenever the session state materially changes.

#include "ablink/session.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/tempo_cv_ledc.hpp"
#include "neon/link_snapshot.hpp"
#include "neon/tempo_cv.hpp"

#include "board_pins.h"
#include "config_store.h"
#include "netman/net_manager.h"
#include "tasks.h"
#include "timeline_bus.h"
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

  neon::TimelineSnapshot prev{};
  bool have_prev = false;
  uint32_t last_logged_peers = UINT32_MAX;
  double last_logged_tempo = 0.0;
  neon::ActiveNet last_net = neon::ActiveNet::kNone;
  bool ap_recommended_logged = false;

  for (;;) {
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
    }
    vTaskDelay(kCapturePeriod);
  }
}

}  // namespace

void neon_start_link_service() {
  xTaskCreatePinnedToCore(link_service_task, "link_svc", 8192, nullptr, 10,
                          nullptr, 0);
}
