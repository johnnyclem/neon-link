// CONFIG_NEON_SYNC: the process-wide session is a Neon Sync node
// (components/neon_sync, docs/NEON_SYNC.md) — the fourth implementation of
// the hal::ILinkSession seam. Everything downstream of the timeline
// snapshot behaves identically; only the wire protocol changed.

#include "ablink/session.hpp"
#include "nsyncesp/session.hpp"

namespace ablink {

hal::ILinkSession& session() { return nsyncesp::session(); }

}  // namespace ablink
