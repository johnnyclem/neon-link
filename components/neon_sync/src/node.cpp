#include "nsync/node.hpp"

#include <cmath>
#include <cstring>

#include "neon/fixed_math.hpp"

namespace nsync {

namespace {

constexpr double kQ32 = 4294967296.0;
constexpr int64_t kTsfFreshUs = 5 * 1000000ll;

int64_t double_beat_to_q32(double beat) {
  return static_cast<int64_t>(beat * kQ32 + (beat >= 0.0 ? 0.5 : -0.5));
}

uint64_t tempo_q32_from_bpm(double bpm) {
  double milli = bpm * 1000.0 + 0.5;
  if (milli < 1000.0) {
    milli = 1000.0;
  }
  if (milli > 999000.0) {
    milli = 999000.0;
  }
  return neon::micros_per_beat_q32_from_milli_bpm(
      static_cast<uint32_t>(milli));
}

// Last-writer-wins: does (seq_a, writer_a) beat (seq_b, writer_b)?
bool lww_wins(uint64_t seq_a, uint64_t writer_a, uint64_t seq_b,
              uint64_t writer_b) {
  return seq_a > seq_b || (seq_a == seq_b && writer_a > writer_b);
}

int64_t clamp_i64(int64_t v, int64_t lo, int64_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

Node::Node(const NodeConfig& cfg) : cfg_(cfg) {
  rng_ = cfg_.node_id ^ 0x9e3779b97f4a7c15ull;
  if (rng_ == 0) {
    rng_ = 0x2545f4914f6cdd1dull;
  }
}

void Node::start(double initial_bpm, int64_t now_us) {
  if (started_ || cfg_.node_id == 0) {
    return;
  }
  started_ = true;
  session_id_ = cfg_.node_id;
  ghost_us_ = 0;  // founder: session time is our local clock
  ghost_locked_ = false;
  last_discipline_us_ = now_us;

  state_.tl.seq = 1;
  state_.tl.writer = cfg_.node_id;
  state_.tl.origin_session_us = static_cast<uint64_t>(session_now(now_us));
  state_.tl.beat_at_origin_q32 = 0;
  state_.tl.tempo_mpb_q32 = tempo_q32_from_bpm(initial_bpm);
  state_.tl.quantum_mb = pending_quantum_mb_;

  // Free-running boxes pulse from boot (matching the Link stub and the
  // Daisy internal timeline); joining a session adopts its transport.
  state_.tr.seq = 1;
  state_.tr.writer = cfg_.node_id;
  state_.tr.playing = 1;
  state_.tr.toggle_session_us = state_.tl.origin_session_us;

  max_seen_tl_seq_ = state_.tl.seq;
  max_seen_tr_seq_ = state_.tr.seq;

  next_announce_us_ = now_us;  // introduce ourselves immediately
  next_tsf_hint_us_ = now_us;
}

void Node::stop(Emitter& out) {
  if (!started_) {
    return;
  }
  uint8_t buf[kMaxPacket];
  const size_t n = encode_bye(header(), buf, sizeof(buf));
  if (n != 0) {
    out.send(Emitter::kMulticast, buf, n);
  }
  started_ = false;
  for (auto& p : peers_) {
    p = Peer{};
  }
}

void Node::handle_packet(const uint8_t* data, size_t len, Addr from,
                         int64_t now_us, Emitter& out) {
  Header h;
  if (!started_ || !decode_header(data, len, h) ||
      h.node_id == cfg_.node_id || h.node_id == 0) {
    return;
  }
  switch (h.type) {
    case MsgType::kAnnounce: {
      AnnounceMsg m;
      if (decode_announce(data, len, m)) {
        on_announce(h, m, from, now_us);
      }
      break;
    }
    case MsgType::kBye: {
      Peer* p = find_peer(h.node_id);
      if (p != nullptr) {
        *p = Peer{};
      }
      break;
    }
    case MsgType::kPing: {
      PingMsg m;
      if (decode_ping(data, len, m)) {
        on_ping(h, m, from, now_us, out);
      }
      break;
    }
    case MsgType::kPong: {
      PongMsg m;
      if (decode_pong(data, len, m)) {
        on_pong(h, m, now_us);
      }
      break;
    }
    case MsgType::kTsfHint: {
      TsfHintMsg m;
      if (decode_tsf_hint(data, len, m)) {
        on_tsf_hint(h, m, now_us);
      }
      break;
    }
  }
}

void Node::set_local_tsf(const uint8_t bssid[6], uint64_t tsf_us,
                         int64_t local_us) {
  have_local_tsf_ = true;
  std::memcpy(local_bssid_, bssid, sizeof(local_bssid_));
  local_tsf_us_ = tsf_us;
  local_tsf_local_us_ = local_us;
  local_tsf_seen_us_ = local_us;
}

void Node::poll(int64_t now_us, Emitter& out) {
  if (!started_) {
    return;
  }

  // Expire peers that stopped announcing.
  for (auto& p : peers_) {
    if (p.in_use) {
      const int64_t ttl_us =
          static_cast<int64_t>(p.ttl_ms != 0 ? p.ttl_ms : cfg_.ttl_ms) * 1000;
      if (now_us - p.last_seen_us > ttl_us) {
        p = Peer{};
      }
    }
  }

  discipline_ghost(now_us);

  // Clock measurement: join burst first, steady cadence after.
  for (auto& p : peers_) {
    if (!p.in_use) {
      continue;
    }
    if (p.ping_outstanding &&
        now_us - static_cast<int64_t>(p.ping_t1_us) >
            static_cast<int64_t>(cfg_.ping_timeout_us)) {
      p.ping_outstanding = false;
    }
    if (!p.ping_outstanding && now_us >= p.next_ping_us) {
      PingMsg m;
      m.t1_local_us = static_cast<uint64_t>(now_us);
      uint8_t buf[kMaxPacket];
      const size_t n = encode_ping(header(), m, buf, sizeof(buf));
      if (n != 0) {
        out.send(p.addr, buf, n);
        p.ping_outstanding = true;
        p.ping_t1_us = m.t1_local_us;
        ++p.pings_sent;
        const uint32_t gap =
            p.pings_sent < cfg_.ping_burst ? cfg_.ping_burst_spacing_us
            : p.pings_sent < cfg_.ping_warmup_count
                ? cfg_.ping_warmup_spacing_us
                : cfg_.ping_interval_us;
        p.next_ping_us = now_us + gap;
      }
    }
  }

  // Share our TSF sample while it is fresh (the radio glue refreshes it).
  if (have_local_tsf_ && now_us - local_tsf_seen_us_ <= kTsfFreshUs &&
      now_us >= next_tsf_hint_us_) {
    TsfHintMsg m;
    std::memcpy(m.bssid, local_bssid_, sizeof(m.bssid));
    m.tsf_us = local_tsf_us_;
    m.local_us = static_cast<uint64_t>(local_tsf_local_us_);
    uint8_t buf[kMaxPacket];
    const size_t n = encode_tsf_hint(header(), m, buf, sizeof(buf));
    if (n != 0) {
      out.send(Emitter::kMulticast, buf, n);
    }
    next_tsf_hint_us_ = now_us + cfg_.tsf_hint_interval_us;
  }

  if (now_us >= next_announce_us_) {
    send_announce(now_us, out);
    const uint32_t j = cfg_.announce_jitter_us;
    const int64_t jitter =
        j != 0 ? static_cast<int64_t>(rand_u32() % (2 * j + 1)) -
                     static_cast<int64_t>(j)
               : 0;
    next_announce_us_ =
        now_us + static_cast<int64_t>(cfg_.announce_interval_us) + jitter;
  }
}

bool Node::capture(hal::LinkState& out, int64_t now_us) const {
  if (!started_) {
    return false;
  }
  const int64_t s_now = session_now(now_us);
  out.origin_us = now_us;
  out.tempo_bpm =
      60000000.0 / (static_cast<double>(state_.tl.tempo_mpb_q32) / kQ32);
  out.beat_at_origin = beat_at_session(s_now);
  out.quantum = static_cast<double>(state_.tl.quantum_mb) / 1000.0;
  out.playing =
      start_stop_sync_ ? transport_playing_at(s_now) : local_playing_;
  out.num_peers = num_peers(now_us);
  return true;
}

void Node::set_tempo(double bpm, int64_t now_us) {
  if (!started_ || bpm <= 0.0) {
    return;
  }
  const uint64_t q32 = tempo_q32_from_bpm(bpm);
  if (q32 == state_.tl.tempo_mpb_q32) {
    return;
  }
  // Re-anchor so the beat grid stays continuous through the change.
  const int64_t s_now = session_now(now_us);
  const double beat = beat_at_session(s_now);
  state_.tl.origin_session_us = static_cast<uint64_t>(s_now);
  state_.tl.beat_at_origin_q32 = double_beat_to_q32(beat);
  state_.tl.tempo_mpb_q32 = q32;
  state_.tl.seq = next_tl_seq();
  state_.tl.writer = cfg_.node_id;
  next_announce_us_ = now_us;
}

void Node::set_playing(bool playing, int64_t now_us) {
  if (!started_) {
    return;
  }
  if (!start_stop_sync_) {
    local_playing_ = playing;
    return;
  }
  const int64_t s_now = session_now(now_us);
  if (transport_playing_at(s_now) == playing) {
    return;
  }
  state_.tr.playing = playing ? 1 : 0;
  state_.tr.toggle_session_us = static_cast<uint64_t>(s_now);
  state_.tr.seq = next_tr_seq();
  state_.tr.writer = cfg_.node_id;
  next_announce_us_ = now_us;
}

void Node::request_beat_at_time(int64_t t_us, int64_t now_us) {
  if (!started_) {
    return;
  }
  // Beat 0 (a quantum boundary) lands at t_us on our clock.
  state_.tl.origin_session_us = static_cast<uint64_t>(t_us + ghost_us_);
  state_.tl.beat_at_origin_q32 = 0;
  state_.tl.seq = next_tl_seq();
  state_.tl.writer = cfg_.node_id;
  next_announce_us_ = now_us;
}

void Node::set_start_stop_sync(bool enable, int64_t now_us) {
  if (enable == start_stop_sync_) {
    return;
  }
  if (!enable && started_) {
    // Freeze the current transport as the private local one.
    local_playing_ = transport_playing_at(session_now(now_us));
  }
  start_stop_sync_ = enable;
}

void Node::set_quantum(double beats, int64_t now_us) {
  if (beats < 1.0 || beats > 16.0) {
    return;
  }
  const uint32_t mb = static_cast<uint32_t>(beats * 1000.0 + 0.5);
  if (!started_) {
    pending_quantum_mb_ = mb;
    return;
  }
  if (mb == state_.tl.quantum_mb) {
    return;
  }
  state_.tl.quantum_mb = mb;
  state_.tl.seq = next_tl_seq();
  state_.tl.writer = cfg_.node_id;
  next_announce_us_ = now_us;
}

uint32_t Node::num_peers(int64_t now_us) const {
  uint32_t n = 0;
  for (const auto& p : peers_) {
    if (p.in_use && peer_live(p, now_us)) {
      ++n;
    }
  }
  return n;
}

bool Node::tsf_active(int64_t now_us) const {
  for (const auto& p : peers_) {
    if (p.in_use && peer_live(p, now_us) && p.clock.using_tsf(now_us)) {
      return true;
    }
  }
  return false;
}

Node::Peer* Node::find_peer(uint64_t node_id) {
  for (auto& p : peers_) {
    if (p.in_use && p.node_id == node_id) {
      return &p;
    }
  }
  return nullptr;
}

const Node::Peer* Node::find_peer(uint64_t node_id) const {
  for (const auto& p : peers_) {
    if (p.in_use && p.node_id == node_id) {
      return &p;
    }
  }
  return nullptr;
}

Node::Peer* Node::upsert_peer(uint64_t node_id, Addr addr, int64_t now_us) {
  Peer* p = find_peer(node_id);
  if (p != nullptr) {
    p->addr = addr;
    return p;
  }
  for (auto& slot : peers_) {
    if (!slot.in_use) {
      slot = Peer{};
      slot.in_use = true;
      slot.node_id = node_id;
      slot.addr = addr;
      slot.last_seen_us = now_us;
      slot.ttl_ms = cfg_.ttl_ms;
      slot.next_ping_us = now_us;  // join burst starts right away
      // A new face means our state is news to someone; announce soon.
      schedule_announce_soon(now_us);
      return &slot;
    }
  }
  return nullptr;  // table full: ignore the 17th peer
}

bool Node::peer_live(const Peer& p, int64_t now_us) const {
  const int64_t ttl_us =
      static_cast<int64_t>(p.ttl_ms != 0 ? p.ttl_ms : cfg_.ttl_ms) * 1000;
  return now_us - p.last_seen_us <= ttl_us;
}

void Node::on_announce(const Header& h, const AnnounceMsg& m, Addr from,
                       int64_t now_us) {
  Peer* p = upsert_peer(h.node_id, from, now_us);
  if (p == nullptr) {
    return;
  }
  p->last_seen_us = now_us;
  if (m.ttl_ms >= 500 && m.ttl_ms <= 60000) {
    p->ttl_ms = m.ttl_ms;
  }
  p->have_announce = true;
  p->announced_ghost_us = m.ghost_offset_us;
  // One-way coarse seed: biased by the transit delay, replaced by the
  // first real ping/pong or TSF sample.
  p->clock.seed(static_cast<int64_t>(m.sender_local_tx_us) - now_us);

  if (m.state.tl.seq > max_seen_tl_seq_) {
    max_seen_tl_seq_ = m.state.tl.seq;
  }
  if (m.state.tr.seq > max_seen_tr_seq_) {
    max_seen_tr_seq_ = m.state.tr.seq;
  }

  bool adopted = false;
  if (lww_wins(m.state.tl.seq, m.state.tl.writer, state_.tl.seq,
               state_.tl.writer)) {
    if (h.session_id != session_id_) {
      // Crossing into another session's time domain: the adopted state's
      // session timestamps only mean something through that domain's
      // mapping, so jump the ghost now (sender's mapping + our offset to
      // the sender — the seed above at worst) instead of slewing from an
      // unrelated domain. This is also what keeps a high-id newcomer from
      // dragging an established session onto its own clock: it lands in
      // the session's domain before anyone could elect it as reference.
      ghost_us_ = m.ghost_offset_us + p->clock.offset_us(now_us);
      ghost_locked_ = true;
    }
    adopt_timeline(m.state.tl, h.session_id);
    adopted = true;
  } else if (m.state.tl.seq == state_.tl.seq &&
             m.state.tl.writer == state_.tl.writer &&
             h.session_id > session_id_) {
    // Same authored state travelling under two ids (island merge with no
    // interleaved writes): unify on the higher id.
    session_id_ = h.session_id;
    adopted = true;
  }
  if (lww_wins(m.state.tr.seq, m.state.tr.writer, state_.tr.seq,
               state_.tr.writer)) {
    adopt_transport(m.state.tr);
    adopted = true;
  }
  if (adopted) {
    schedule_announce_soon(now_us);  // gossip the outcome
  }
}

void Node::on_ping(const Header& h, const PingMsg& m, Addr from,
                   int64_t now_us, Emitter& out) {
  (void)h;
  PongMsg r;
  r.t1_echo_us = m.t1_local_us;
  r.t2_remote_rx_us = static_cast<uint64_t>(now_us);
  r.t3_remote_tx_us = static_cast<uint64_t>(now_us);
  uint8_t buf[kMaxPacket];
  const size_t n = encode_pong(header(), r, buf, sizeof(buf));
  if (n != 0) {
    out.send(from, buf, n);
  }
}

void Node::on_pong(const Header& h, const PongMsg& m, int64_t now_us) {
  Peer* p = find_peer(h.node_id);
  if (p == nullptr || !p->ping_outstanding || m.t1_echo_us != p->ping_t1_us) {
    return;
  }
  p->ping_outstanding = false;
  const int64_t t1 = static_cast<int64_t>(m.t1_echo_us);
  const int64_t t2 = static_cast<int64_t>(m.t2_remote_rx_us);
  const int64_t t3 = static_cast<int64_t>(m.t3_remote_tx_us);
  const int64_t t4 = now_us;
  const int64_t rtt = (t4 - t1) - (t3 - t2);
  const int64_t offset = ((t2 - t1) + (t3 - t4)) / 2;
  p->clock.add_measured(offset, rtt, now_us);
  // A pong is proof of life even if announces are being lost.
  p->last_seen_us = now_us;
}

void Node::on_tsf_hint(const Header& h, const TsfHintMsg& m, int64_t now_us) {
  Peer* p = find_peer(h.node_id);
  if (p == nullptr) {
    return;  // wait for the announce; hints alone don't admit a peer
  }
  p->have_tsf = true;
  std::memcpy(p->bssid, m.bssid, sizeof(p->bssid));
  p->tsf_minus_local_us = static_cast<int64_t>(m.tsf_us) -
                          static_cast<int64_t>(m.local_us);
  p->tsf_seen_us = now_us;
  // Same BSS on both sides -> the AP beacon clock is shared, and the
  // offset is a plain difference of (tsf - local) terms.
  if (have_local_tsf_ && now_us - local_tsf_seen_us_ <= kTsfFreshUs &&
      std::memcmp(local_bssid_, m.bssid, sizeof(local_bssid_)) == 0) {
    const int64_t local_b = static_cast<int64_t>(local_tsf_us_) -
                            local_tsf_local_us_;
    p->clock.add_tsf(local_b - p->tsf_minus_local_us, now_us);
  }
}

void Node::adopt_timeline(const TimelineState& tl, uint64_t from_session) {
  state_.tl = tl;
  session_id_ = from_session;
  if (tl.seq > max_seen_tl_seq_) {
    max_seen_tl_seq_ = tl.seq;
  }
}

void Node::adopt_transport(const TransportState& tr) {
  state_.tr = tr;
  if (tr.seq > max_seen_tr_seq_) {
    max_seen_tr_seq_ = tr.seq;
  }
}

void Node::discipline_ghost(int64_t now_us) {
  // Reference = highest node id among live announcing peers and self.
  // Deterministic from membership: no election traffic, and churn just
  // moves the reference with a bounded slew at every follower.
  const Peer* ref = nullptr;
  uint64_t ref_id = cfg_.node_id;
  for (const auto& p : peers_) {
    if (p.in_use && p.have_announce && peer_live(p, now_us) &&
        p.node_id > ref_id) {
      ref = &p;
      ref_id = p.node_id;
    }
  }
  ghost_ref_id_ = ref_id;
  const int64_t dt = clamp_i64(now_us - last_discipline_us_, 0, 1000000);
  last_discipline_us_ = now_us;
  if (ref == nullptr) {
    // We are the reference: hold the mapping steady (session-time
    // continuity is exactly the founder pinning its ghost).
    return;
  }
  if (!ref->clock.valid(now_us)) {
    return;
  }
  const int64_t target =
      ref->announced_ghost_us + ref->clock.offset_us(now_us);
  const int64_t err = target - ghost_us_;
  if (!ghost_locked_ ||
      err > static_cast<int64_t>(cfg_.ghost_snap_threshold_us) ||
      err < -static_cast<int64_t>(cfg_.ghost_snap_threshold_us)) {
    set_ghost(target);
    ghost_locked_ = true;
    return;
  }
  // PI servo, slewed — never stepped: the beat grid must move smoothly
  // under the engine. P pulls a fraction of the error per tau_p so
  // per-measurement noise is filtered instead of copied into the grid; I
  // learns the persistent error slope (relative crystal drift) so the
  // ghost keeps advancing correctly between measurement updates.
  constexpr int64_t kTauPUs = 1500000;             // P time constant
  constexpr int64_t kTauI2UsS = 25000000;          // (5 s)^2 as µs·s
  constexpr int64_t kMaxRateQ16PerS = 200ll << 16;  // ±200 µs/s of drift
  const int64_t err_q16 = err * 65536;
  ghost_rate_q16_per_s_ =
      clamp_i64(ghost_rate_q16_per_s_ + err_q16 * dt / kTauI2UsS,
                -kMaxRateQ16PerS, kMaxRateQ16PerS);
  const int64_t p_q16 = err_q16 * dt / kTauPUs;
  const int64_t max_step_q16 =
      (static_cast<int64_t>(cfg_.ghost_slew_us_per_s) << 16) * dt / 1000000;
  const int64_t step_q16 =
      clamp_i64(p_q16 + ghost_rate_q16_per_s_ * dt / 1000000, -max_step_q16,
                max_step_q16);
  ghost_q16_ += step_q16;
  ghost_us_ = ghost_q16_ / 65536;
}

void Node::set_ghost(int64_t ghost_us) {
  ghost_us_ = ghost_us;
  ghost_q16_ = ghost_us * 65536;
}

void Node::send_announce(int64_t now_us, Emitter& out) {
  AnnounceMsg m;
  m.state = state_;
  m.ghost_offset_us = ghost_us_;
  m.sender_local_tx_us = static_cast<uint64_t>(now_us);
  m.ttl_ms = cfg_.ttl_ms;
  uint8_t buf[kMaxPacket];
  const size_t n = encode_announce(header(), m, buf, sizeof(buf));
  if (n != 0) {
    out.send(Emitter::kMulticast, buf, n);
  }
}

void Node::schedule_announce_soon(int64_t now_us) {
  const int64_t soon =
      now_us + static_cast<int64_t>(rand_u32() % 200001);  // 0..200 ms
  if (soon < next_announce_us_) {
    next_announce_us_ = soon;
  }
}

uint64_t Node::next_tl_seq() {
  if (state_.tl.seq > max_seen_tl_seq_) {
    max_seen_tl_seq_ = state_.tl.seq;
  }
  return ++max_seen_tl_seq_;
}

uint64_t Node::next_tr_seq() {
  if (state_.tr.seq > max_seen_tr_seq_) {
    max_seen_tr_seq_ = state_.tr.seq;
  }
  return ++max_seen_tr_seq_;
}

double Node::beat_at_session(int64_t session_us) const {
  const double mpb_us = static_cast<double>(state_.tl.tempo_mpb_q32) / kQ32;
  if (mpb_us <= 0.0) {
    return 0.0;
  }
  const int64_t d =
      session_us - static_cast<int64_t>(state_.tl.origin_session_us);
  return static_cast<double>(state_.tl.beat_at_origin_q32) / kQ32 +
         static_cast<double>(d) / mpb_us;
}

bool Node::transport_playing_at(int64_t session_us) const {
  const bool playing = state_.tr.playing != 0;
  return session_us >= static_cast<int64_t>(state_.tr.toggle_session_us)
             ? playing
             : !playing;
}

Header Node::header() const {
  Header h;
  h.proto_ver = kProtoVersion;
  h.flags = 0;
  h.node_id = cfg_.node_id;
  h.session_id = session_id_;
  return h;
}

uint32_t Node::rand_u32() {
  // xorshift64* — deterministic per node id, good enough for jitter.
  rng_ ^= rng_ >> 12;
  rng_ ^= rng_ << 25;
  rng_ ^= rng_ >> 27;
  return static_cast<uint32_t>((rng_ * 0x2545f4914f6cdd1dull) >> 32);
}

}  // namespace nsync
