#include "link_service_t41.h"

#include <Arduino.h>

#include "ablink/session.hpp"
#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "neon/ext_clock.hpp"
#include "neon/link_snapshot.hpp"
#include "neon/tempo_cv.hpp"
#include "neon/transport.hpp"

#include "board_pins_t41.h"
#include "clkin_t41.h"
#include "timebase_t41.h"

// ableton/platforms/teensy41/Runtime.hpp; declared here so this file
// does not pull QNEthernet into its translation unit.
extern "C" void neon_t41_link_poll();

namespace linksvc {
namespace {

constexpr int64_t kCapturePeriodUs = 10000;  // 10 ms, like the ESP task

neon::TapTempo g_tap;
neon::TransportLatch g_latch;
neon::ExtClockEstimator g_ext_clock;
bool g_local_playing = false;
bool g_ext_active = false;
int64_t g_next_capture_us = 0;
neon::TimelineSnapshot g_prev{};
bool g_have_prev = false;

void apply_session_settings(hal::ILinkSession& session) {
  const auto& cfg = neon_config();
  session.set_start_stop_sync(cfg.start_stop_sync != 0);
  session.set_quantum(static_cast<double>(cfg.quantum_beats));
}

void update_tempo_cv(uint32_t milli_bpm) {
  const auto& cfg = neon_config();
  if (cfg.midi.pitch_cv) {
    return;  // BLE-MIDI pitch CV owns the jack on ESP; inert here
  }
  const uint16_t ratio = neon::tempo_cv_ratio_q16(
      milli_bpm, cfg.tempo_cv_min_bpm, cfg.tempo_cv_max_bpm);
  analogWrite(kPinTempoCv, ratio >> 8);
}

void drain_control_queue(hal::ILinkSession& session, int64_t now) {
  neon::TimelineSnapshot tl{};
  timeline_bus().read(tl);
  ControlCommand cmd;
  while (control_queue_pop(&cmd)) {
    neon::Config next = neon_config();
    bool cfg_dirty = false;
    switch (cmd.kind) {
      case ControlCommand::Kind::kPlay:
        g_latch.request(tl, now, true);
        break;
      case ControlCommand::Kind::kStop:
        g_latch.request(tl, now, false);
        break;
      case ControlCommand::Kind::kToggle:
        g_latch.request(tl, now, !g_local_playing);
        break;
      case ControlCommand::Kind::kPlayNow:
        g_latch.request(tl, now, true, /*quantized=*/false);
        break;
      case ControlCommand::Kind::kStopNow:
        g_latch.request(tl, now, false, /*quantized=*/false);
        break;
      case ControlCommand::Kind::kSetTempo:
        next.tempo_milli_bpm =
            neon::clamp_milli_bpm(static_cast<int64_t>(cmd.arg));
        cfg_dirty = true;
        break;
      case ControlCommand::Kind::kNudgeTempo:
        next.tempo_milli_bpm = neon::nudge_milli_bpm(next.tempo_milli_bpm,
                                                     cmd.arg);
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
        if (g_tap.tap(now, &mbpm)) {
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
        session.request_beat_at_time(neon::resync_target_us(tl, now, mode));
        break;
      }
    }
    if (cfg_dirty) {
      session.set_tempo(static_cast<double>(next.tempo_milli_bpm) / 1000.0);
      neon_config_apply(next);
    }
  }
  bool want_play = false;
  if (g_latch.poll(now, &want_play)) {
    session.set_playing(want_play);
    g_local_playing = want_play;
  }
}

void follow_external_clock(hal::ILinkSession& session, int64_t now) {
  clkin::Event ev;
  while (clkin::pop(&ev)) {
    if (ev.kind == clkin::Kind::kClock) {
      g_ext_clock.on_pulse(ev.t_us);
    } else {
      g_ext_clock.on_reset(ev.t_us);
    }
  }
  g_ext_clock.set_input_ppqn(neon_config().clock_in_ppqn);
  const neon::ClockSource source = neon_config().clock_source;
  const bool follow = source != neon::ClockSource::kLinkMaster &&
                      g_ext_clock.active(now);
  if (follow != g_ext_active) {
    g_ext_active = follow;
    app_status_set_ext_clock(follow);
  }
  if (follow) {
    uint32_t mbpm = 0;
    if (g_ext_clock.take_tempo_update(&mbpm)) {
      session.set_tempo(static_cast<double>(mbpm) / 1000.0);
    }
    int64_t downbeat_us = 0;
    if (g_ext_clock.take_phase_request(&downbeat_us)) {
      session.request_beat_at_time(downbeat_us);
    }
  } else {
    uint32_t scratch_t = 0;
    int64_t scratch_p = 0;
    g_ext_clock.take_tempo_update(&scratch_t);
    g_ext_clock.take_phase_request(&scratch_p);
  }
}

}  // namespace

void init(int64_t now_us) {
  clkin::init(kPinClkIn, kPinRstIn);
  g_ext_clock.set_input_ppqn(neon_config().clock_in_ppqn);

  auto& session = ablink::session();
  apply_session_settings(session);
  session.start(static_cast<double>(neon_config().tempo_milli_bpm) / 1000.0);
  Serial.println("link: session started");
  update_tempo_cv(neon_config().tempo_milli_bpm);
  g_next_capture_us = now_us;
}

void poll(int64_t now_us) {
  // The platform runtime (sockets, timers, posted jobs) pumps every
  // call; the session capture/publish work runs on the 10 ms cadence.
  neon_t41_link_poll();

  if (now_us < g_next_capture_us) {
    return;
  }
  g_next_capture_us = now_us + kCapturePeriodUs;

  auto& session = ablink::session();
  apply_session_settings(session);
  drain_control_queue(session, now_us);
  follow_external_clock(session, now_us);

  hal::LinkState state;
  if (session.capture(state)) {
    neon::TimelineSnapshot snap;
    if (neon::build_snapshot(state, g_have_prev ? &g_prev : nullptr, snap)) {
      timeline_bus().publish(snap);
      g_prev = snap;
      g_have_prev = true;
    }
    static uint32_t last_peers = UINT32_MAX;
    static double last_tempo = 0.0;
    if (state.num_peers != last_peers || state.tempo_bpm != last_tempo) {
      update_tempo_cv(static_cast<uint32_t>(state.tempo_bpm * 1000.0));
      last_peers = state.num_peers;
      last_tempo = state.tempo_bpm;
    }
    app_status_set_peers(state.num_peers);
    // A peer (or start/stop sync) can move the transport under us; keep
    // the local view in step so Toggle does the right thing.
    g_local_playing = state.playing;
    app_status_set_transport(state.playing);
  }
  neon_config_flush(now_us);
}

bool playing() { return g_local_playing; }

}  // namespace linksvc
