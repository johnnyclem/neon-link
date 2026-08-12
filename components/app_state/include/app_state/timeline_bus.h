#pragma once

#include <cstdint>

#include "neon/multi_engine.hpp"
#include "neon/timeline.hpp"

// The seqlock buses and status flags shared between core-0 services
// (Link, UI, web) and the core-1 pulse engine.

// Link timeline: written by the Link service, read by core 1.
neon::SeqLock<neon::TimelineSnapshot>& timeline_bus();

// Engine configuration: written on config changes (UI/web), read by
// core 1, which re-applies and re-anchors on a new version.
neon::SeqLock<neon::EngineConfig>& engine_config_bus();

// Read-mostly status for the UI (atomics under the hood).
void app_status_set_peers(uint32_t peers);
uint32_t app_status_peers();
void app_status_set_ext_clock(bool active);
bool app_status_ext_clock();

// MIDI note gates: core-0 router -> core-1 pulse task, which emits the
// edge on the target channel's GPIO with the usual scheduling lead.
struct GateEvent {
  uint8_t channel;  // neon::Channel index (0..3 clocks, 5 run)
  bool on;
};
bool gate_queue_push(const GateEvent& ev);
bool gate_queue_pop(GateEvent* ev);

// Transport / tempo commands from any core-0 producer (web editor, OLED
// encoder, BLE MIDI) to the Link service, which owns the session and is
// the only thing allowed to talk to it. Small lock-free ring.
struct ControlCommand {
  enum class Kind : uint8_t {
    kPlay = 0,       // start the transport at the next loop boundary
    kStop = 1,       // stop at the next loop boundary
    kToggle = 2,
    kPlayNow = 3,    // unquantized, for "start on my downbeat"
    kStopNow = 4,
    kSetTempo = 5,   // arg = milli-BPM
    kNudgeTempo = 6, // arg = signed whole BPM
    kDoubleTempo = 7,
    kHalveTempo = 8,
    kTapTempo = 9,   // stamped with the arrival time by the service
    kResyncNextLoop = 10,
    kResyncNow = 11,
  };
  Kind kind;
  int32_t arg;
};
bool control_queue_push(const ControlCommand& cmd);
bool control_queue_pop(ControlCommand* cmd);

// Live transport/tempo status published by the Link service for the
// editor's status endpoint and the OLED.
void app_status_set_transport(bool playing);
bool app_status_transport();
