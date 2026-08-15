#pragma once

#include "hal/ILinkSession.hpp"

// The session seam: the link service is the single owner of whatever
// implements this, and never knows which one it got.
//
//   link_session_daisy.cpp   the InternalTimeline adapter — the three
//                            offline configs (no network hardware).
//   link_session_netlink.cpp the real ableton::Link peer over USB
//                            gadget networking (daisy/netlink).
//
// The per-config Makefile picks the implementation (SESSION_SRC in
// daisy/boards.mk).
hal::ILinkSession& daisy_session();

// Pumped by the link service every poll: the netlink build's Link
// runtime (sockets, timers, posted jobs) hangs off this; the offline
// configs leave the weak default no-op.
extern "C" void neon_daisy_link_pump() __attribute__((weak));
