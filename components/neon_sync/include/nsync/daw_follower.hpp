#pragma once

// The DAW bridge for Neon Sync (docs/NEON_SYNC.md §7.4, SPIKE §5.1): maps a
// VST3/AU host's AudioPlayHead onto nsync::Node writes so the mesh follows
// the DAW. Sans-I/O like the node itself — the plugin's socket loop feeds
// playhead samples and the local clock in; every mesh effect goes out
// through the node's own announce path — so the whole policy is testable
// under the host simulator (host/tests/test_nsync_follower.cpp).
//
// Policy ("the DAW is authoritative while its transport runs"):
//  - DAW playing + drive on: tempo is level-asserted, the beat grid is
//    re-anchored onto the DAW's bars when the phase error persists beyond
//    tolerance, and the mesh transport is held running. A mesh-side edit
//    is corrected (rate-limited), because the DAW cannot follow the mesh —
//    VST3 has no way to set host tempo (that bridge is Max for Live's job).
//  - DAW stopped + drive on: only edges write — a DAW tempo change or a
//    transport stop/start transition. Mesh-side edits stand.
//  - Drive off: pure monitor; the node still participates in the session
//    (discovery, clock discipline, gossip) but this class never writes.
//
// The follower also never writes while it has no live peers: alone there is
// nothing to sync, and staying silent lets the first device's announce win
// the seq-1 tie so the plugin adopts the session's quantum and time domain
// instead of imposing its own defaults.
//
// Concurrency: none here, matching Node — the owner serializes update()
// and status() with everything else touching the node. The one lock-free
// piece, PlayheadMailbox, is the audio-thread hand-off and lives apart.

#include <cstdint>

#include "nsync/node.hpp"

namespace nsync {

// One AudioPlayHead observation, stamped against the caller's local
// microsecond clock at the audio callback that saw it.
struct DawPlayhead {
  bool valid = false;      // the host reported a position this callback
  bool playing = false;    // host transport state
  double bpm = 0.0;        // host tempo; 0 when the host gave none
  double beat = 0.0;       // host PPQ position, quarter notes
  int64_t sampled_us = 0;  // local clock at the observation
};

struct FollowerConfig {
  // A sample older than this means the host stopped calling us (engine
  // idle, playhead withheld): the DAW is treated as absent, edge tracking
  // re-baselines when it comes back.
  int64_t stale_after_us = 500000;
  // Phase error beyond this, phase_debounce updates in a row, re-anchors
  // the grid. Callback timestamp jitter under host load is O(1 ms); the
  // mesh itself holds < 500 µs, so 3 ms separates real drift from noise.
  int64_t phase_tolerance_us = 3000;
  uint32_t phase_debounce = 3;
  // Minimum gap between corrective writes (tempo correction, transport
  // correction, grid re-anchor) so a mesh-side edit war or a host tempo
  // ramp becomes a bounded announce rate, not a storm.
  int64_t correction_gap_us = 250000;
  double tempo_epsilon_bpm = 0.005;
  // Tempo agreement required before a phase comparison means anything.
  double phase_tempo_window_bpm = 0.1;
  // Hold all writes while the node sees no live peers (see header note).
  bool require_peer = true;
};

// Snapshot for the editor: the session as the node sees it, the DAW as the
// follower last saw it, and how the two grids relate.
struct FollowerStatus {
  bool session_up = false;
  double session_bpm = 0.0;
  bool session_playing = false;
  uint32_t peers = 0;
  double quantum = 4.0;
  uint64_t session_id = 0;

  bool drive = false;
  bool daw_fresh = false;
  bool daw_playing = false;
  double daw_bpm = 0.0;

  bool phase_valid = false;   // the last update could compare the grids
  int64_t phase_err_us = 0;   // session grid minus DAW grid, µs
  bool phase_locked = false;  // within tolerance on the last comparison
};

class DawFollower {
 public:
  explicit DawFollower(Node& node, const FollowerConfig& cfg = {})
      : node_(node), cfg_(cfg) {}

  // Drive = "the DAW writes the mesh". Off is pure monitoring.
  void set_drive(bool enabled);
  bool drive() const { return drive_; }

  // Feed the freshest playhead sample; call every service tick (~10 ms).
  void update(const DawPlayhead& ph, int64_t now_us);

  FollowerStatus status(int64_t now_us) const;

 private:
  bool phase_error(const DawPlayhead& ph, const hal::LinkState& st,
                   int64_t now_us, int64_t* err_us) const;
  // Places a session quantum boundary on the DAW's own bar line.
  void align_grid(const DawPlayhead& ph, const hal::LinkState& st,
                  int64_t now_us);
  bool can_correct(int64_t now_us) const;
  void mark_correction(int64_t now_us);

  Node& node_;
  FollowerConfig cfg_;
  bool drive_ = true;

  // Edge tracking across fresh samples.
  bool have_last_ = false;
  bool last_playing_ = false;
  double last_daw_bpm_ = 0.0;

  bool have_correction_ = false;
  int64_t last_correction_us_ = 0;
  uint32_t phase_strikes_ = 0;

  // Latest observations, for status().
  bool daw_fresh_ = false;
  bool daw_playing_ = false;
  double daw_bpm_ = 0.0;
  bool phase_valid_ = false;
  int64_t phase_err_us_ = 0;
  bool phase_locked_ = false;
};

}  // namespace nsync
