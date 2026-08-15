#pragma once

// Runtime override for the FreeRTOS priorities Link's asio service task
// (Context.hpp's ServiceRunner) and the Link Audio pump task
// (link_audio_esp.cpp) run at.
//
// Both tasks read these instead of a hardcoded number. main/app_main.cpp
// sets them once, right after the config loads and before any task that
// could touch them is created — set_link_asio_priority in particular must
// land before the Link session (and therefore ServiceRunner's lazily
// constructed singleton) is first touched, so the setters are not safe to
// call from within a running task race with task creation.
//
// This exists to reproduce the pre-fix priority inversion on demand
// (neon::PriorityProfile::kLegacy) for regression testing — see
// docs/STUDIO_MODE_TEST_PLAN.md Phase 2 — without a separate firmware
// build. Normal operation always uses the kFixed defaults below.

namespace ablink {

// Defaults match the shipped fix: asio above the timeline poll (10) and
// the pump (9).
void set_link_asio_priority(int priority);
int link_asio_priority();

void set_link_pump_priority(int priority);
int link_pump_priority();

}  // namespace ablink
