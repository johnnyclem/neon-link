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
  // `at_us` is when the change takes effect (esp_timer / session clock).
  // Negative means "now". Quantized play/stop must pass the loop boundary
  // so start/stop sync advertises the scheduled transition instead of
  // waiting until the bar line and losing the race against the session.
  virtual void set_playing(bool playing, int64_t at_us = -1) = 0;

  // Ask the session to place a quantum boundary (beat 0 mod quantum) at
  // the given time — used when RST IN provides an external downbeat.
  virtual void request_beat_at_time(int64_t t_us) = 0;

  // Link 3 start/stop sync: follow (and broadcast) transport changes from
  // other peers. Users turn this off to keep a local transport private.
  virtual void set_start_stop_sync(bool enable) = 0;

  // Loop size in beats. Drives phase quantization and the beat the module
  // reports, so the editor's "Loop Size" reaches the session itself.
  virtual void set_quantum(double beats) = 0;
};

}  // namespace hal
