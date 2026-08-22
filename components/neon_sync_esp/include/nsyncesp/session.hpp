#pragma once

#include "hal/ILinkSession.hpp"

namespace nsyncesp {

// The process-wide Neon Sync session: an nsync::Node driven by a socket
// task on core 0. Same interface and downstream behavior as the Ableton
// Link session it replaces (CONFIG_NEON_SYNC).
hal::ILinkSession& session();

}  // namespace nsyncesp
