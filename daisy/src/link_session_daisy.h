#pragma once

#include "hal/ILinkSession.hpp"

#include "internal_timeline.h"

// hal::ILinkSession over the InternalTimeline — the Daisy Seed has no
// network interface, so there is no Ableton Link session to bind
// (docs/DAISY.md §1). Keeping the seam means link_service_daisy.cpp
// stays line-identical to the other targets' service loops, and a future
// networked variant (USB gadget networking) slots in behind the same
// interface.
namespace tsession {

class TimelineSession : public hal::ILinkSession {
 public:
  void start(double initial_bpm) override;
  bool capture(hal::LinkState& out) override;
  void set_tempo(double bpm) override;
  void set_playing(bool playing) override;
  void request_beat_at_time(int64_t t_us) override;
  void set_start_stop_sync(bool enable) override;
  void set_quantum(double beats) override;

 private:
  InternalTimeline tl_;
  bool started_ = false;
};

// The one session instance (the Daisy stand-in for ablink::session()).
TimelineSession& session();

}  // namespace tsession
