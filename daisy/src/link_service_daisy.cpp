#include "link_service_daisy.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "neon/ext_clock.hpp"
#include "neon/link_snapshot.hpp"
#include "neon/midi/sync_follower.hpp"
#include "neon/tempo_cv.hpp"
#include "neon/transport.hpp"

#include "board_daisy.h"
#include "board_pins_daisy.h"
#include "clkin_daisy.h"
#include "midi_daisy.h"
#include "session_daisy.h"
#include "timebase_daisy.h"

namespace linksvc {
namespace {

constexpr int64_t kCapturePeriodUs = 10000;  // 10 ms, like the ESP task

neon::TapTempo g_tap;
neon::TransportLatch g_latch;
neon::ExtClockEstimator g_ext_clock;
// Incoming MIDI clock is the second external sync source: the router's
// sync tap (midi_daisy.cpp) feeds this follower's PLL, and its actions
// steer the session. The CLK IN jack outranks it when both are alive.
neon::midi::SyncFollower g_midi_follow;
// This capture's arbitration verdict, consumed by the publish stage:
// while following, the PLL model is published as the snapshot.
bool g_midi_following = false;
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
    return;  // MIDI pitch CV owns the jack (same rule as the ESP/Teensy)
  }
  const uint16_t ratio = neon::tempo_cv_ratio_q16(
      milli_bpm, cfg.tempo_cv_min_bpm, cfg.tempo_cv_max_bpm);
  board_tempo_cv_write(ratio);
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

  // Arbitration (docs/SPIKE_MIDI_PLL.md §5.4): CLK IN outranks MIDI
  // clock under kAuto — the jack is this module's native sync, and a
  // beat-locked drum machine sending both would otherwise fight itself.
  // Each master mode pins its own source; the session is the fallback.
  // While not allowed, the follower's PLL keeps tracking silently so a
  // handover starts from a warm estimate.
  const neon::ClockSource source = neon_config().clock_source;
  const bool jack = (source == neon::ClockSource::kAuto ||
                     source == neon::ClockSource::kExternalMaster) &&
                    g_ext_clock.active(now);
  const bool midi_allowed = (source == neon::ClockSource::kAuto ||
                             source == neon::ClockSource::kMidiMaster) &&
                            !jack;
  const neon::midi::SyncFollower::Actions midi_act =
      g_midi_follow.poll(now, midi_allowed);
  g_midi_following = midi_act.following;

  const bool follow = jack || midi_act.following;
  if (follow != g_ext_active) {
    g_ext_active = follow;
    app_status_set_ext_clock(follow);
  }
  if (jack) {
    uint32_t mbpm = 0;
    if (g_ext_clock.take_tempo_update(&mbpm)) {
      session.set_tempo(static_cast<double>(mbpm) / 1000.0);
    }
    int64_t downbeat_us = 0;
    if (g_ext_clock.take_phase_request(&downbeat_us)) {
      session.request_beat_at_time(downbeat_us);
    }
  } else {
    // Consume stale one-shots so they don't fire on reactivation.
    uint32_t scratch_t = 0;
    int64_t scratch_p = 0;
    g_ext_clock.take_tempo_update(&scratch_t);
    g_ext_clock.take_phase_request(&scratch_p);
  }
  if (midi_act.set_tempo) {
    session.set_tempo(static_cast<double>(midi_act.tempo_mbpm) / 1000.0);
  }
  if (midi_act.anchor_downbeat) {
    session.request_beat_at_time(midi_act.downbeat_us);
  }
  if (midi_act.set_playing) {
    session.set_playing(midi_act.playing);
    g_local_playing = midi_act.playing;
  }
}

}  // namespace

void init(int64_t now_us) {
  clkin::init(kPinClkIn, kPinRstIn);
  g_ext_clock.set_input_ppqn(neon_config().clock_in_ppqn);
  miditrs::set_sync_follower(&g_midi_follow);

  auto& session = daisy_session();
  apply_session_settings(session);
  session.start(static_cast<double>(neon_config().tempo_milli_bpm) / 1000.0);
  update_tempo_cv(neon_config().tempo_milli_bpm);
  g_next_capture_us = now_us;
}

void poll(int64_t now_us) {
  // The Link runtime (netlink build: sockets, timers, posted jobs)
  // pumps every call; the capture/publish work runs on the 10 ms
  // cadence. No-op on the offline configs.
  if (&neon_daisy_link_pump != nullptr) {
    neon_daisy_link_pump();
  }

  if (now_us < g_next_capture_us) {
    return;
  }
  g_next_capture_us = now_us + kCapturePeriodUs;

  auto& session = daisy_session();
  apply_session_settings(session);
  drain_control_queue(session, now_us);
  follow_external_clock(session, now_us);

  hal::LinkState state;
  if (session.capture(state)) {
    // While following MIDI clock with no peers, the PLL *is* the session
    // (docs/MIDI_PLL_PHASES_HANDOFF.md Phase C): its model maps 1:1 onto
    // the snapshot, so the outputs ride the continuous estimate instead
    // of the hysteretic set_tempo steps the session was steered with.
    // With Link peers (the netlink build) the session consensus owns the
    // grid and the steered capture stands, like the ESP path.
    neon::MidiClockPll::Model m{};
    if (g_midi_following && state.num_peers == 0 &&
        g_midi_follow.pll().model(&m)) {
      state.tempo_bpm =
          60000000.0 / (static_cast<double>(m.tempo_mpb_q32) / 4294967296.0);
      if (m.beat_valid) {
        // Position, not just tempo: unlike the Link path (which only
        // anchors the downbeat), the model's musical position is exact —
        // SPP and bar position land on the sender's grid.
        state.origin_us = m.origin_us;
        state.beat_at_origin =
            static_cast<double>(m.beat_at_origin_q32) / 4294967296.0;
        state.playing = m.playing;
      }
      // While !beat_valid (free clock, no Start yet): tempo only — the
      // captured session phase keeps the local beat continuous.
    }
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
    // The transport can move under us (quantized latch fire); keep the
    // local view in step so Toggle does the right thing.
    g_local_playing = state.playing;
    app_status_set_transport(state.playing);
  }
  neon_config_flush(now_us);
}

bool playing() { return g_local_playing; }

}  // namespace linksvc
