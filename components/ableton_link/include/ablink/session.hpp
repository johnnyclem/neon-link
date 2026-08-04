#pragma once

#include "hal/ILinkSession.hpp"

namespace ablink {

// The process-wide Link session: the real Ableton Link peer, or the
// free-running stub when CONFIG_NEON_LINK_STUB is set. Same interface,
// same downstream behavior.
hal::ILinkSession& session();

}  // namespace ablink
