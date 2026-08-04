#pragma once

#include "neon/timeline.hpp"

// The single seqlock connecting core 0 (Link service, writer) to core 1
// (pulse engine, reader).
neon::SeqLock<neon::TimelineSnapshot>& timeline_bus();
