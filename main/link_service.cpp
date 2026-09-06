// Core 0: the Ableton Link service task. Brings up WiFi (if configured),
// starts the Link session, and publishes integer timeline snapshots to the
// pulse engine whenever the session state materially changes.

#include <cstddef>
#include <cstdio>

#include "sdkconfig.h"

#include "ablink/session.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/clkin_capture.hpp"
#include "halesp/tempo_cv_ledc.hpp"
#include "neon/audio/tempo_follower.hpp"
#include "neon/clock_arbitration.hpp"
#include "neon/ext_clock.hpp"
#include "neon/link_snapshot.hpp"
#include "neon/midi/sync_follower.hpp"
#include "neon/telemetry/audio_follow_csv.hpp"
#include "neon/telemetry/emitter.hpp"
#include "neon/telemetry/midi_pll_csv.hpp"
#include "neon/tempo_cv.hpp"
#include "neon/transport.hpp"

#include "board_mac.h"
#include "board_pins.h"
#include "app_state/audio_bus.h"
#include "app_state/config_store.h"
#include "netman/net_manager.h"
#include "provision.h"
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
  neon_read_unit_mac(mac);
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

#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
// Hosted UART blocks in esp_wifi_init until the C5 sends ESPInit — up
// to ~20 s, or forever if the coprocessor never talks. RUN/STOP live on
// this task's control-queue loop, so SoftAP must not run here.
// XIAO EN is not on the header, so a missed ESPInit is recovered by
// retrying after the user taps RST on the C5.
bool g_hosted_ap_kicked = false;
int64_t g_hosted_retry_us = 0;

void hosted_ap_task(void*) {
  if (start_ap_from_config()) {
    ESP_LOGI(kTag, "C5 up; setup AP is on the air");
  } else {
    ESP_LOGW(kTag, "P4 LCD: SoftAP failed; retry after C5 RST");
    g_hosted_retry_us = esp_timer_get_time() + 3000000;
    g_hosted_ap_kicked = false;
  }
  vTaskDelete(nullptr);
}

void kick_hosted_ap() {
  if (netman::wifi_driver_ready() || g_hosted_ap_kicked) {
    return;
  }
  if (esp_timer_get_time() < g_hosted_retry_us) {
    return;
  }
  g_hosted_ap_kicked = true;
  ESP_LOGI(kTag, "C5 Hosted AP starting in background");
  if (xTaskCreate(hosted_ap_task, "hosted_ap", 8192, nullptr, 5, nullptr) !=
      pdPASS) {
    g_hosted_ap_kicked = false;
    ESP_LOGW(kTag, "hosted_ap task create failed; SoftAP skipped");
  }
}
#endif

void bring_up_setup_ap() {
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
  kick_hosted_ap();
#else
  start_ap_from_config();
#endif
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

  neon_provision_start();

#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
  // Hosted UART to the XIAO C5 (DIP=WM). Onboard C6 is not used.
  // Kick SoftAP off-thread so the local Link session starts even if
  // the C5 never INIT's — otherwise RUN queues a toggle nobody pops.
  kick_hosted_ap();
#elif CONFIG_NEON_BOARD_LINKSYNC_TAB5
  // Hosted only talks to the C6 inside esp_wifi_init (ap_start).
  if (start_ap_from_config()) {
    ESP_LOGI(kTag, "C6 up; setup AP is on the air");
  } else {
    ESP_LOGW(kTag, "P4 LCD: SoftAP failed; local Link only");
  }
#else
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
      // Do not sit on OFF for another minute: 192.168.4.1 is what the
      // user tries next, and it does not exist until SoftAP is up.
      if (neon_config().ap_policy == neon::ApPolicy::kFallback) {
        ESP_LOGW(kTag, "STA never got an address; holding STA and starting setup AP");
        neon_wifi_hold_station();
        bring_up_setup_ap();
      }
    }
  } else if (neon_config().ap_policy == neon::ApPolicy::kFallback) {
    // First boot / no stored STA. On link-sync, BLE provision is the
    // primary path and SoftAP waits 90 s (neon_provision_poll).
    if (neon_provision_active()) {
      ESP_LOGI(kTag, "no STA credentials; BLE provision running (SoftAP in 90 s)");
    } else {
      ESP_LOGI(kTag, "no STA credentials; starting setup AP");
      bring_up_setup_ap();
    }
  }
#endif

  auto& session = ablink::session();
  apply_session_settings(session);
  session.start(static_cast<double>(neon_config().tempo_milli_bpm) / 1000.0);
#if CONFIG_NEON_SYNC
  ESP_LOGI(kTag, "Neon Sync session started (Nearby; no Ableton Link)");
#else
  ESP_LOGI(kTag, "Link session started");
#endif

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
  neon::midi::SyncFollower midi_follow;
  neon::AudioTempoFollower audio_follow;
  neon::TelemetryTicker afol_ticker(/*ticks_per_line=*/100);  // 10 ms * 100 = 1 Hz
  uint8_t last_afol_lock = 0;
  uint32_t onset_window_count = 0;
  int64_t onset_window_us = 0;
  uint16_t onset_hz_x10 = 0;
  // The session as of the previous loop's capture (10 ms stale at most),
  // feeding the follower's Link-authority policy (spike §8.4): while the
  // MIDI transport runs, peer tempo edits and peer stops are corrected;
  // while stopped, peer edits stand. require_peer stays at its default
  // (off) — this module's session is also its local timeline, so MIDI
  // follow must work with zero peers.
  neon::midi::SessionView midi_session;
  bool ext_active = false;
  // PLL CSV behind debug.telemetry_uart_csv, tagged "PLL,". Dual-core
  // boards can afford one row per 0xF8 (~48/s at 120 BPM) on a real UART.
  // Unicore (C3) shares the core with USB Serial/JTAG: a 50 ms/char spin
  // when the host stops draining wedges MIDI for the rest of the session.
#if CONFIG_FREERTOS_UNICORE
  neon::TelemetryTicker pll_ticker(/*ticks_per_line=*/48);  // ~1 Hz at 120 BPM
#else
  neon::TelemetryTicker pll_ticker(/*ticks_per_line=*/1);
#endif

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
      bool transport_dirty = false;
      while (control_queue_pop(&cmd)) {
        neon::Config next = neon_config();
        bool cfg_dirty = false;
        switch (cmd.kind) {
          case ControlCommand::Kind::kPlay:
            latch.request(tl, now, true);
            transport_dirty = true;
            break;
          case ControlCommand::Kind::kStop:
            latch.request(tl, now, false);
            transport_dirty = true;
            break;
          case ControlCommand::Kind::kToggle:
            latch.request(tl, now, !local_playing);
            transport_dirty = true;
            break;
          case ControlCommand::Kind::kPlayNow:
          case ControlCommand::Kind::kStopNow:
            // A MIDI 0xFA/0xFC also reaches the follower through the sync
            // tap; while it owns transport its edge-tracked set_playing is
            // the one write the session gets, and the router's unquantized
            // duplicate is dropped. Panel/editor commands (from_midi
            // clear) always pass.
            if (cmd.from_midi && midi_follow.following()) {
              break;
            }
            latch.request(tl, now,
                          cmd.kind == ControlCommand::Kind::kPlayNow,
                          /*quantized=*/false);
            transport_dirty = true;
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
      // Tell Link about the scheduled start/stop at the press, using the
      // loop-boundary timestamp. Waiting until the bar line to call
      // setIsPlaying(false, now) loses to start/stop sync: the session
      // still advertises playing, and the late stop is overwritten.
      if (transport_dirty && latch.armed()) {
        session.set_playing(latch.pending_play(), latch.fire_at_us());
      }
      bool want_play = false;
      if (latch.poll(now, &want_play)) {
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
    const int64_t now_arb = esp_timer_get_time();
    const neon::ClockArbitration arb = neon::arbitrate_clock_source(
        neon_config().clock_source, ext_clock.active(now_arb),
        neon_config().audio_follow_enabled != 0);
    const bool follow_external = arb.follow_clk_in;

    neon::midi::SyncEvent mev;
    while (midi_sync_queue_pop(&mev)) {
      midi_follow.on_event(mev);
      if (mev.kind == neon::midi::SyncEvent::Kind::kTick) {
        const neon::TelemetryTick tt =
            pll_ticker.tick(neon_config().telemetry_uart_csv != 0);
        char buf[128];
        if (tt.want_header &&
            neon::midi_pll_telemetry_csv_header(buf, sizeof(buf)) != 0) {
          printf("PLL,%s\n", buf);
        }
        if (tt.want_line) {
          // following() is last poll's verdict — at most 10 ms stale,
          // fine for a yes/no analysis column.
          const neon::MidiPllTelemetrySample s = neon::midi_pll_telemetry_sample(
              midi_follow.pll(), mev.t_us, midi_follow.following());
          if (neon::midi_pll_telemetry_csv_line(s, buf, sizeof(buf)) != 0) {
            printf("PLL,%s\n", buf);
          }
        }
      }
    }
    const neon::midi::SyncFollower::Actions midi_act =
        midi_follow.poll(now_arb, arb.midi_allowed, midi_session);

    const auto& cfg_now = neon_config();
    uint32_t session_mbpm = cfg_now.tempo_milli_bpm;
    if (have_prev && prev.tempo_mpb_q32 != 0) {
      const uint64_t mpb_us = (prev.tempo_mpb_q32 + (1ull << 31)) >> 32;
      if (mpb_us != 0) {
        session_mbpm = neon::milli_bpm_from_mpb_us(mpb_us);
      }
    }
    neon::OnsetEvent oe;
    uint32_t popped = 0;
    while (onset_queue_pop(&oe)) {
      ++popped;
      audio_follow.set_session_tempo(session_mbpm);
      audio_follow.on_onset(oe.t_us, oe.strength);
      if (cfg_now.audio_follow_phase) {
        /* reserved until kAdcLatencyUs benched */
      }
    }
    audio_follow.active(now_arb);
    // audio_ok: permission (arb.audio_allowed) and MIDI is not live.
    // midi_allowed stays true under kAuto so the PLL can stay warm.
    const bool audio_ok = arb.audio_allowed && !midi_act.following;
    uint32_t audio_mbpm = 0;
    if (audio_ok && audio_follow.take_tempo_update(&audio_mbpm)) {
      session.set_tempo(static_cast<double>(audio_mbpm) / 1000.0);
      ESP_LOGI(kTag, "audio tempo -> %u BPM",
               static_cast<unsigned>(audio_mbpm / 1000));
    }
    if (!audio_ok) {
      uint32_t scratch = 0;
      audio_follow.take_tempo_update(&scratch);
    }

    onset_window_count += popped;
    if (onset_window_us == 0) {
      onset_window_us = now_arb;
    }
    if (now_arb - onset_window_us >= 1000000) {
      const int64_t dt = now_arb - onset_window_us;
      onset_hz_x10 = static_cast<uint16_t>(
          (static_cast<uint64_t>(onset_window_count) * 10ull * 1000000ull) /
          static_cast<uint64_t>(dt));
      onset_window_count = 0;
      onset_window_us = now_arb;
    }

    neon::FollowStatus fst;
    fst.enabled = cfg_now.audio_follow_enabled;
    fst.lock = static_cast<uint8_t>(audio_follow.lock_state());
    fst.subdiv = audio_follow.subdivision();
    fst.no_adc = (kPinI2sDin < 0 || follow_no_adc()) ? 1 : 0;
    fst.onset_hz_x10 = onset_hz_x10;
    fst.mbpm = audio_follow.estimate_milli_bpm();
    fst.published_mbpm = audio_follow.tempo_milli_bpm();
    fst.onsets = audio_follow.onset_count();
    fst.rejects = audio_follow.rejected_count();
    follow_status_bus().publish(fst);

    if (fst.lock != last_afol_lock) {
      ESP_LOGI(kTag, "audio follow %s",
               fst.lock == 2   ? "locked"
               : fst.lock == 1 ? "acquiring"
                               : "idle");
      last_afol_lock = fst.lock;
    }

    const neon::TelemetryTick aft =
        afol_ticker.tick(cfg_now.telemetry_uart_csv != 0 &&
                         cfg_now.audio_follow_enabled != 0);
    if (aft.want_header || aft.want_line) {
      char buf[128];
      if (aft.want_header &&
          neon::audio_follow_telemetry_csv_header(buf, sizeof(buf)) != 0) {
        printf("AFOL,%s\n", buf);
      }
      if (aft.want_line) {
        const neon::AudioFollowTelemetrySample s =
            neon::audio_follow_telemetry_sample(fst, now_arb, audio_ok &&
                                                fst.lock == 2);
        if (neon::audio_follow_telemetry_csv_line(s, buf, sizeof(buf)) != 0) {
          printf("AFOL,%s\n", buf);
        }
      }
    }

    const bool audio_following =
        audio_ok &&
        audio_follow.lock_state() == neon::AudioTempoFollower::Lock::kLocked;
    const bool any_external =
        follow_external || midi_act.following || audio_following;
    if (any_external != ext_active) {
      ESP_LOGI(kTag, "external clock %s",
               !any_external          ? "lost"
               : follow_external      ? "active: following CLK IN"
               : midi_act.following   ? "active: following MIDI clock"
                                      : "active: following audio");
      ext_active = any_external;
      app_status_set_ext_clock(any_external);
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
    if (midi_act.set_tempo) {
      ESP_LOGI(kTag, "MIDI tempo -> %u BPM",
               static_cast<unsigned>(midi_act.tempo_mbpm / 1000));
      session.set_tempo(static_cast<double>(midi_act.tempo_mbpm) / 1000.0);
    }
    if (midi_act.anchor_downbeat) {
      ESP_LOGI(kTag, "MIDI: anchoring downbeat");
      session.request_beat_at_time(midi_act.downbeat_us);
    }
    if (midi_act.set_playing) {
      ESP_LOGI(kTag, "MIDI transport -> %s",
               midi_act.playing ? "play" : "stop");
      session.set_playing(midi_act.playing);
      local_playing = midi_act.playing;
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
        !netman::ap_is_up() &&
        netman::preference().update_should_start_ap(esp_timer_get_time()) &&
        !ap_recommended_logged) {
      ESP_LOGW(kTag, "no connectivity: holding STA and starting setup AP");
      neon_wifi_hold_station();
      bring_up_setup_ap();
      ap_recommended_logged = true;
    }
    hal::LinkState state;
    midi_session = {};
    if (session.capture(state)) {
      midi_session.valid = true;
      midi_session.tempo_mbpm =
          static_cast<uint32_t>(state.tempo_bpm * 1000.0 + 0.5);
      midi_session.playing = state.playing;
      midi_session.peers = state.num_peers;
      midi_session.have_timeline = true;
      midi_session.beat_at_origin = state.beat_at_origin;
      midi_session.origin_us = state.origin_us;
      midi_session.quantum_beats =
          state.quantum >= 1.0 ? static_cast<uint32_t>(state.quantum) : 4u;
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
    {
      const int64_t now = esp_timer_get_time();
      if (neon_provision_poll(now) &&
          neon_config().ap_policy == neon::ApPolicy::kFallback &&
          !netman::ap_is_up()) {
        bring_up_setup_ap();
      }
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD
      if (!netman::wifi_driver_ready()) {
        kick_hosted_ap();
      }
#endif
      neon_config_flush(now);
    }
    vTaskDelay(kCapturePeriod);
  }
}

}  // namespace

void neon_start_link_service() {
  xTaskCreatePinnedToCore(link_service_task, "link_svc", 8192, nullptr, 10,
                          nullptr, 0);
}
