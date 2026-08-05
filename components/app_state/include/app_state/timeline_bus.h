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
