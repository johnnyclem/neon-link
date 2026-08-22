#pragma once

// The Neon Sync protocol engine (docs/NEON_SYNC.md). Sans-I/O: the node
// never touches a socket or a clock — callers feed it received packets and
// the current local time, and poll() hands outgoing packets to an Emitter.
// That makes the whole protocol host-testable (host/tests drive N nodes
// through a lossy, delayed, clock-skewed fake network) and keeps the ESP
// glue (components/neon_sync_esp) down to a socket loop.
//
// The local API deliberately mirrors hal::ILinkSession, because that is
// the contract the firmware consumes; the ESP glue is a thin adapter.
//
// Concurrency: none here. The owner serializes all calls (the ESP glue
// wraps the node in a mutex; the host tests are single-threaded).

#include <cstddef>
#include <cstdint>

#include "hal/ILinkSession.hpp"
#include "nsync/peer_clock.hpp"
#include "nsync/wire.hpp"

namespace nsync {

// Opaque unicast address (IPv4 in the ESP glue, arbitrary in tests).
using Addr = uint32_t;

// poll()/stop() hand outgoing packets to this. dest == kMulticast means
// the group; anything else is a unicast address previously seen on rx.
class Emitter {
 public:
  static constexpr Addr kMulticast = 0;
  virtual ~Emitter() = default;
  virtual void send(Addr dest, const uint8_t* data, size_t len) = 0;
};

struct NodeConfig {
  uint64_t node_id = 0;  // must be nonzero and unique (MAC-derived)
  uint32_t announce_interval_us = 1000000;
  uint32_t announce_jitter_us = 200000;  // ± applied to each interval
  uint32_t ttl_ms = 3500;                // advertised peer timeout
  uint32_t ping_interval_us = 500000;
  uint32_t ping_burst = 4;  // pings at join, spaced burst_spacing apart
  uint32_t ping_burst_spacing_us = 250000;
  // After the burst, ping faster until the sample window has enough depth
  // for the quartile filter to bite, then settle to ping_interval.
  uint32_t ping_warmup_count = 16;
  uint32_t ping_warmup_spacing_us = 500000;
  uint32_t ping_timeout_us = 500000;
  uint32_t tsf_hint_interval_us = 2000000;
  // Ghost discipline: slew bound (never step once locked) and the error
  // beyond which slewing is hopeless and we snap instead.
  uint32_t ghost_slew_us_per_s = 1000;
  uint32_t ghost_snap_threshold_us = 100000;
};

class Node {
 public:
  static constexpr size_t kMaxPeers = 16;

  explicit Node(const NodeConfig& cfg);

  // --- lifecycle -----------------------------------------------------
  void start(double initial_bpm, int64_t now_us);
  bool started() const { return started_; }
  // Emits BYE so peers drop us immediately instead of waiting out the TTL.
  void stop(Emitter& out);

  // --- network input -------------------------------------------------
  // `out` carries immediate replies (a PING is answered with a PONG from
  // inside the call so t2/t3 are honest receive/transmit times).
  void handle_packet(const uint8_t* data, size_t len, Addr from,
                     int64_t now_us, Emitter& out);

  // Radio glue feeds the local (bssid, tsf, local) sample when associated;
  // enables the TSF fast path against peers on the same BSS.
  void set_local_tsf(const uint8_t bssid[6], uint64_t tsf_us,
                     int64_t local_us);

  // --- periodic work -------------------------------------------------
  // Expires peers, disciplines the ghost offset, emits due ANNOUNCE /
  // PING / TSF_HINT traffic. Call every ~10 ms.
  void poll(int64_t now_us, Emitter& out);

  // --- ILinkSession-shaped local API ----------------------------------
  // All setters deduplicate no-op changes: only a material change bumps
  // the Lamport seq and re-announces (the service layer calls some of
  // these every tick).
  bool capture(hal::LinkState& out, int64_t now_us) const;
  void set_tempo(double bpm, int64_t now_us);
  void set_playing(bool playing, int64_t now_us);
  void request_beat_at_time(int64_t t_us, int64_t now_us);
  void set_start_stop_sync(bool enable, int64_t now_us);
  void set_quantum(double beats, int64_t now_us);

  // --- introspection (status document, tests) -------------------------
  uint32_t num_peers(int64_t now_us) const;
  uint64_t session_id() const { return session_id_; }
  const SessionState& state() const { return state_; }
  // session_us = local_us + ghost_offset_us().
  int64_t ghost_offset_us() const { return ghost_us_; }
  // True while any live peer's offset comes from the TSF fast path.
  bool tsf_active(int64_t now_us) const;

 private:
  struct Peer {
    uint64_t node_id = 0;
    Addr addr = 0;
    bool in_use = false;
    int64_t last_seen_us = 0;
    uint32_t ttl_ms = 0;
    bool have_announce = false;
    int64_t announced_ghost_us = 0;

    PeerClock clock;

    // Ping state machine: join burst, then steady cadence.
    uint32_t pings_sent = 0;
    int64_t next_ping_us = 0;
    bool ping_outstanding = false;
    uint64_t ping_t1_us = 0;

    // Latest TSF hint from this peer.
    bool have_tsf = false;
    uint8_t bssid[6] = {0, 0, 0, 0, 0, 0};
    int64_t tsf_minus_local_us = 0;
    int64_t tsf_seen_us = 0;
  };

  Peer* find_peer(uint64_t node_id);
  const Peer* find_peer(uint64_t node_id) const;
  Peer* upsert_peer(uint64_t node_id, Addr addr, int64_t now_us);
  bool peer_live(const Peer& p, int64_t now_us) const;

  void on_announce(const Header& h, const AnnounceMsg& m, Addr from,
                   int64_t now_us);
  void on_ping(const Header& h, const PingMsg& m, Addr from, int64_t now_us,
               Emitter& out);
  void on_pong(const Header& h, const PongMsg& m, int64_t now_us);
  void on_tsf_hint(const Header& h, const TsfHintMsg& m, int64_t now_us);

  void adopt_timeline(const TimelineState& tl, uint64_t from_session);
  void adopt_transport(const TransportState& tr);

  void discipline_ghost(int64_t now_us);
  void send_announce(int64_t now_us, Emitter& out);
  void schedule_announce_soon(int64_t now_us);

  uint64_t next_tl_seq();
  uint64_t next_tr_seq();

  int64_t session_now(int64_t now_us) const { return now_us + ghost_us_; }
  double beat_at_session(int64_t session_us) const;
  bool transport_playing_at(int64_t session_us) const;

  Header header() const;
  uint32_t rand_u32();

  NodeConfig cfg_;
  bool started_ = false;

  uint64_t session_id_ = 0;
  SessionState state_;
  uint64_t max_seen_tl_seq_ = 0;
  uint64_t max_seen_tr_seq_ = 0;

  // Session-time mapping for this node. The session founder pins its ghost
  // at 0 (session time *is* its local clock); everyone else servoes toward
  // (reference peer's announced ghost + measured offset to that peer),
  // where the reference is the highest node id among live peers and self —
  // deterministic from membership, no election traffic. The servo is PI in
  // Q48.16 µs: the proportional term low-passes estimate noise, the
  // integral term learns the relative crystal drift so the mapping keeps
  // moving correctly between measurement updates.
  void set_ghost(int64_t ghost_us);
  int64_t ghost_us_ = 0;
  int64_t ghost_q16_ = 0;               // fine ghost, Q48.16 µs
  int64_t ghost_rate_q16_per_s_ = 0;    // learned drift, Q.16 µs/s
  bool ghost_locked_ = false;
  uint64_t ghost_ref_id_ = 0;
  int64_t last_discipline_us_ = 0;

  bool start_stop_sync_ = true;
  bool local_playing_ = true;  // transport when start/stop sync is off

  uint32_t pending_quantum_mb_ = 4000;  // set_quantum before start()

  int64_t next_announce_us_ = 0;
  int64_t next_tsf_hint_us_ = 0;

  // Own TSF sample from the radio glue (raw pair kept for re-broadcast).
  bool have_local_tsf_ = false;
  uint8_t local_bssid_[6] = {0, 0, 0, 0, 0, 0};
  uint64_t local_tsf_us_ = 0;
  int64_t local_tsf_local_us_ = 0;
  int64_t local_tsf_seen_us_ = 0;

  Peer peers_[kMaxPeers];
  uint64_t rng_;
};

}  // namespace nsync
