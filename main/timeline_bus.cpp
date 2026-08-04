#include "timeline_bus.h"

neon::SeqLock<neon::TimelineSnapshot>& timeline_bus() {
  static neon::SeqLock<neon::TimelineSnapshot> bus;
  return bus;
}
