#pragma once

#include <cstdint>

namespace hal {

// Session state as captured from Ableton Link (or a stand-in). Doubles are
// allowed here: this struct only exists at the core-0 boundary, where
// neon::build_snapshot converts it to the integer TimelineSnapshot before
// anything real-time sees it.
struct LinkState {
  double tempo_bpm = 120.0;
  double beat_at_origin = 0.0;  // session beat at origin_us
  int64_t origin_us = 0;        // capture time (esp_timer domain)
  double quantum = 4.0;
  bool playing = false;
  uint32_t num_peers = 0;
};

class ILinkSession {
 public:
  virtual ~ILinkSession() = default;

  // Bring the session up (idempotent). Call after the network is available
  // when possible; Link still forms a local session without one.
  virtual void start(double initial_bpm) = 0;

  // Capture current session state. Returns false before start().
  virtual bool capture(LinkState& out) = 0;

  virtual void set_tempo(double bpm) = 0;
  virtual void set_playing(bool playing) = 0;

  // Ask the session to place a quantum boundary (beat 0 mod quantum) at
  // the given time — used when RST IN provides an external downbeat.
  virtual void request_beat_at_time(int64_t t_us) = 0;
};

}  // namespace hal
