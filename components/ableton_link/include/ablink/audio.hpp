#pragma once

#include "hal/ILinkAudio.hpp"

namespace ablink {

// The process-wide Link Audio facade. Backed by the real
// ableton::LinkAudio when CONFIG_NEON_LINK_AUDIO is set (which needs the
// Link-4.0 submodule), and by a no-op otherwise — available() is how
// callers tell, and the UI says so rather than pretending.
//
// One ableton::LinkAudio instance backs both this and ablink::session();
// existing callers of session() are unaffected either way.
hal::ILinkAudio& link_audio();

// Starts the core-0 pump task that moves audio between the rings and the
// network. Idempotent, and a no-op for the stub.
void link_audio_start_pump();

}  // namespace ablink
