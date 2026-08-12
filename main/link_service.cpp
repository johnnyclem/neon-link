// Core 0: the Ableton Link service task. Brings up WiFi (if configured),
// starts the Link session, and publishes integer timeline snapshots to the
// pulse engine whenever the session state materially changes.

#include <cstddef>

#include "ablink/session.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/clkin_capture.hpp"
#include "halesp/tempo_cv_ledc.hpp"
#include "neon/ext_clock.hpp"
#include "neon/link_snapshot.hpp"
#include "neon/tempo_cv.hpp"
#include "neon/transport.hpp"

#include "board_pins.h"
#include "app_state/config_store.h"
#include "netman/net_manager.h"
#include "tasks.h"
#include "app_state/timeline_bus.h"
#include "webui/web_ui.h"
#include "wifi.h"

namespace {

const char* kTag = "link_svc";
constexpr TickType_t kCapturePeriod = pdMS_TO_TICKS(10);
constexpr uint32_t kWifiWaitMs = 15000;

// Access point parameters resolved from the stored config (SSID falls
// back to "<DEVICE-NAME>-XXXX" from the SoftAP MAC).
netman::ApParams ap_params_from_config(char* ssid_buf, size_t cap) {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  const auto& cfg = neon_config();
  neon::ap_ssid_for(cfg, mac, ssid_buf, cap);
  netman::ApParams p{};
  p.ssid = ssid_buf;
  p.pass = cfg.ap_pass;
  p.require_pass = cfg.ap_require_pass != 0;
  p.hidden = cfg.ap_hidden != 0;
  p.channel = cfg.ap_channel;
  return p;
}

bool start_ap_from_config() {
  char ssid[33] = {};
  return netman::ap_start(ap_params_from_config(ssid, sizeof(ssid)));
}

// Apply the config fields the Link session itself owns. Cheap enough to
// call every tick; the session implementations ignore no-op changes.
void apply_session_settings(hal::ILinkSession& session) {
  const auto& cfg = neon_config();
  session.set_start_stop_sync(cfg.start_stop_sync != 0);
  session.set_quantum(static_cast<double>(cfg.quantum_beats));
}

void link_service_task(void*) {
  netman::init_common();
  netman::preference().wifi_configured(neon_wifi_has_credentials());
  // Ethernet first: when a cable is present it outranks WiFi (route
  // priority), and Link's interface scanner picks it up automatically.
  netman::ethernet_start();

  // Bring the editor up *before* the long STA wait so (a) setup works while
  // WiFi is still associating and (b) OTA rollback is cancelled quickly if
  // app_main's early mark_valid was skipped for any reason.
  netman::mdns_start(neon_config().device_name);
  webui_start();

  if (neon_config().ap_policy == neon::ApPolicy::kAlways) {
    // "Always create an access point": self-host immediately and never
    // join a stored network.
    ESP_LOGI(kTag, "AP policy = always; hosting our own network");
    start_ap_from_config();
  } else if (neon_wifi_has_credentials()) {
    neon_wifi_start();
    if (!neon_wifi_wait_ip(kWifiWaitMs)) {
      ESP_LOGW(kTag, "no IP after %lu ms; starting Link anyway (local session)",
               static_cast<unsigned long>(kWifiWaitMs));
    }
  }

  auto& session = ablink::session();
  apply_session_settings(session);
  session.start(static_cast<double>(neon_config().tempo_milli_bpm) / 1000.0);
  ESP_LOGI(kTag, "Link session started");

  neon::TapTempo tap;
  neon::TransportLatch latch;
  bool local_playing = false;

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
    apply_session_settings(session);

    // Transport / tempo commands from the editor, the encoder, or MIDI.
    // The Link session is single-owner: everything else queues here.
    {
      neon::TimelineSnapshot tl{};
      timeline_bus().read(tl);
      const int64_t now = esp_timer_get_time();
      ControlCommand cmd;
      while (control_queue_pop(&cmd)) {
        neon::Config next = neon_config();
        bool cfg_dirty = false;
        switch (cmd.kind) {
          case ControlCommand::Kind::kPlay:
            latch.request(tl, now, true);
            break;
          case ControlCommand::Kind::kStop:
            latch.request(tl, now, false);
            break;
          case ControlCommand::Kind::kToggle:
            latch.request(tl, now, !local_playing);
            break;
          case ControlCommand::Kind::kPlayNow:
            latch.request(tl, now, true, /*quantized=*/false);
            break;
          case ControlCommand::Kind::kStopNow:
            latch.request(tl, now, false, /*quantized=*/false);
            break;
          case ControlCommand::Kind::kSetTempo:
            next.tempo_milli_bpm =
                neon::clamp_milli_bpm(static_cast<int64_t>(cmd.arg));
            cfg_dirty = true;
            break;
          case ControlCommand::Kind::kNudgeTempo:
            next.tempo_milli_bpm =
                neon::nudge_milli_bpm(next.tempo_milli_bpm, cmd.arg);
            cfg_dirty = true;
            break;
          case ControlCommand::Kind::kDoubleTempo:
            next.tempo_milli_bpm = neon::double_milli_bpm(next.tempo_milli_bpm);
            cfg_dirty = true;
            break;
          case ControlCommand::Kind::kHalveTempo:
            next.tempo_milli_bpm = neon::halve_milli_bpm(next.tempo_milli_bpm);
            cfg_dirty = true;
            break;
          case ControlCommand::Kind::kTapTempo: {
            uint32_t mbpm = 0;
            if (tap.tap(now, &mbpm)) {
              next.tempo_milli_bpm = mbpm;
              cfg_dirty = true;
            }
            break;
          }
          case ControlCommand::Kind::kResyncNextLoop:
          case ControlCommand::Kind::kResyncNow: {
            const neon::ResyncMode mode =
                cmd.kind == ControlCommand::Kind::kResyncNow
                    ? neon::ResyncMode::kNow
                    : neon::ResyncMode::kNextLoop;
            session.request_beat_at_time(
                neon::resync_target_us(tl, now, mode));
            ESP_LOGI(kTag, "resync (%s)",
                     mode == neon::ResyncMode::kNow ? "now" : "next loop");
            break;
          }
        }
        if (cfg_dirty) {
          session.set_tempo(static_cast<double>(next.tempo_milli_bpm) / 1000.0);
          neon_config_apply(next);
          ESP_LOGI(kTag, "tempo -> %u.%03u BPM",
                   static_cast<unsigned>(next.tempo_milli_bpm / 1000),
                   static_cast<unsigned>(next.tempo_milli_bpm % 1000));
        }
      }
      bool want_play = false;
      if (latch.poll(now, &want_play)) {
        session.set_playing(want_play);
        local_playing = want_play;
        ESP_LOGI(kTag, "transport -> %s", want_play ? "play" : "stop");
      }
    }

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
    if (neon_config().ap_policy == neon::ApPolicy::kFallback &&
        netman::preference().update_should_start_ap(esp_timer_get_time()) &&
        !ap_recommended_logged) {
      ESP_LOGW(kTag, "no connectivity: starting setup AP");
      start_ap_from_config();
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
        // When BLE-MIDI pitch CV owns the jack, tempo does not drive it.
        if (!cfg.midi.pitch_cv) {
          halesp::tempo_cv_set_ratio(neon::tempo_cv_ratio_q16(
              static_cast<uint32_t>(state.tempo_bpm * 1000.0),
              cfg.tempo_cv_min_bpm, cfg.tempo_cv_max_bpm));
        }
        last_logged_peers = state.num_peers;
        last_logged_tempo = state.tempo_bpm;
      }
      app_status_set_peers(state.num_peers);
      // A peer (or Link start/stop sync) can move the transport under us;
      // keep the local view in step so Toggle does the right thing.
      local_playing = state.playing;
      app_status_set_transport(state.playing);
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
