#pragma once

// In-process Neon Sync network simulator: N nsync::Node instances joined
// by a fake UDP segment with configurable delay, jitter, and loss, each
// node on its own skewed and drifting local clock. This is how discovery,
// agreement, and clock discipline are tested in CI without radios
// (docs/NEON_SYNC.md §6). Fully deterministic: everything random comes
// from one seeded xorshift generator.

#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

#include "nsync/node.hpp"

namespace fakenet {

class Rng {
 public:
  explicit Rng(uint64_t seed) : s_(seed != 0 ? seed : 1) {}
  uint32_t next() {
    s_ ^= s_ >> 12;
    s_ ^= s_ << 25;
    s_ ^= s_ >> 27;
    return static_cast<uint32_t>((s_ * 0x2545f4914f6cdd1dull) >> 32);
  }
  // Uniform in [lo, hi].
  int64_t range(int64_t lo, int64_t hi) {
    return lo + static_cast<int64_t>(next() %
                                     static_cast<uint64_t>(hi - lo + 1));
  }
  bool chance_pct(uint32_t pct) { return next() % 100 < pct; }

 private:
  uint64_t s_;
};

// local_us = true_us + offset + drift. Offsets are arbitrary (nodes boot
// at different times); drift models crystal tolerance in ppm.
struct ClockModel {
  int64_t offset_us = 0;
  int32_t drift_ppm = 0;
  int64_t local(int64_t true_us) const {
    return true_us + offset_us + true_us * drift_ppm / 1000000;
  }
};

class FakeNet {
 public:
  struct Params {
    int64_t base_delay_us = 200;  // one-way propagation
    int64_t jitter_us = 0;        // uniform extra one-way delay [0, jitter]
    uint32_t loss_pct = 0;        // per-delivery drop probability
    int64_t poll_interval_us = 10000;  // matches the 10 ms service tick
    int64_t step_us = 250;
  };

  explicit FakeNet(uint64_t seed) : rng_(seed) {}

  Params params;

  // Nodes with index < partition_boundary can't reach those >= it while
  // partitioned — used to form two islands and then merge them.
  void set_partition(size_t boundary, bool active) {
    partition_boundary_ = boundary;
    partitioned_ = active;
  }

  // Shared-AP TSF: every node samples (bssid, tsf, local) each
  // `interval`, with `read_jitter` of measurement noise on the pair.
  void enable_shared_tsf(const uint8_t bssid[6], int64_t tsf_offset_us,
                         int64_t interval_us = 500000,
                         int64_t read_jitter_us = 25) {
    tsf_enabled_ = true;
    std::memcpy(tsf_bssid_, bssid, 6);
    tsf_offset_us_ = tsf_offset_us;
    tsf_interval_us_ = interval_us;
    tsf_read_jitter_us_ = read_jitter_us;
  }

  // Returns the node's address on the fake segment (1-based).
  nsync::Addr add_node(nsync::Node* node, ClockModel clk) {
    endpoints_.push_back(Endpoint{node, clk, true, 0});
    return static_cast<nsync::Addr>(endpoints_.size());
  }

  // A silenced node neither sends nor receives (crash, not clean BYE).
  void silence(size_t idx, bool silenced) {
    endpoints_[idx].active = !silenced;
  }

  int64_t now_true() const { return now_true_us_; }
  int64_t local_time(size_t idx) const {
    return endpoints_[idx].clk.local(now_true_us_);
  }
  nsync::Node& node(size_t idx) { return *endpoints_[idx].node; }

  void run_until(int64_t t_true_end) {
    while (now_true_us_ < t_true_end) {
      now_true_us_ += params.step_us;
      deliver_due();
      for (size_t i = 0; i < endpoints_.size(); ++i) {
        Endpoint& ep = endpoints_[i];
        if (!ep.active) {
          continue;
        }
        const int64_t local = ep.clk.local(now_true_us_);
        if (tsf_enabled_ && now_true_us_ >= ep.next_tsf_true_us) {
          const int64_t tsf =
              now_true_us_ + tsf_offset_us_ +
              rng_.range(-tsf_read_jitter_us_, tsf_read_jitter_us_);
          ep.node->set_local_tsf(tsf_bssid_, static_cast<uint64_t>(tsf),
                                 local);
          ep.next_tsf_true_us = now_true_us_ + tsf_interval_us_;
        }
        if (now_true_us_ >= ep.next_poll_true_us) {
          TxCapture tx(this, i);
          ep.node->poll(local, tx);
          ep.next_poll_true_us = now_true_us_ + params.poll_interval_us;
        }
      }
    }
  }

  void run_for(int64_t dt_true_us) { run_until(now_true_us_ + dt_true_us); }

  // Beat difference between two nodes at this instant, in µs of the
  // shared tempo — the number the studio-mode harness measures with pulses
  // and a scope. Nodes on one converged timeline agree on the absolute
  // beat, so no modulo is needed.
  int64_t phase_error_us(size_t a, size_t b) {
    hal::LinkState sa, sb;
    if (!endpoints_[a].node->capture(sa, local_time(a)) ||
        !endpoints_[b].node->capture(sb, local_time(b))) {
      return INT64_MAX;
    }
    const double mpb_us = 60000000.0 / sa.tempo_bpm;
    const double d = (sa.beat_at_origin - sb.beat_at_origin) * mpb_us;
    return static_cast<int64_t>(d >= 0 ? d + 0.5 : d - 0.5);
  }

  double tempo_bpm(size_t idx) {
    hal::LinkState s;
    if (!endpoints_[idx].node->capture(s, local_time(idx))) {
      return 0.0;
    }
    return s.tempo_bpm;
  }

 private:
  struct Endpoint {
    nsync::Node* node;
    ClockModel clk;
    bool active;
    int64_t next_poll_true_us;
    int64_t next_tsf_true_us = 0;
  };

  struct InFlight {
    size_t dest;
    nsync::Addr from;
    int64_t deliver_true_us;
    std::vector<uint8_t> data;
  };

  // Captures a node's outgoing packets and fans them out with per-copy
  // loss and jitter (multicast really is per-receiver on WiFi).
  class TxCapture : public nsync::Emitter {
   public:
    TxCapture(FakeNet* net, size_t src) : net_(net), src_(src) {}
    void send(nsync::Addr dest, const uint8_t* data, size_t len) override {
      net_->enqueue(src_, dest, data, len);
    }

   private:
    FakeNet* net_;
    size_t src_;
  };

  bool reachable(size_t a, size_t b) const {
    if (!partitioned_) {
      return true;
    }
    return (a < partition_boundary_) == (b < partition_boundary_);
  }

  void enqueue(size_t src, nsync::Addr dest, const uint8_t* data,
               size_t len) {
    const nsync::Addr from = static_cast<nsync::Addr>(src + 1);
    for (size_t j = 0; j < endpoints_.size(); ++j) {
      if (j == src || !endpoints_[j].active || !reachable(src, j)) {
        continue;
      }
      if (dest != nsync::Emitter::kMulticast &&
          dest != static_cast<nsync::Addr>(j + 1)) {
        continue;
      }
      if (params.loss_pct != 0 && rng_.chance_pct(params.loss_pct)) {
        continue;
      }
      const int64_t delay =
          params.base_delay_us +
          (params.jitter_us != 0 ? rng_.range(0, params.jitter_us) : 0);
      in_flight_.push_back(
          InFlight{j, from, now_true_us_ + delay,
                   std::vector<uint8_t>(data, data + len)});
    }
  }

  void deliver_due() {
    // Deliveries can trigger sends (PING -> PONG), so drain into a local
    // batch first and let new packets queue behind it.
    std::vector<InFlight> due;
    for (size_t i = 0; i < in_flight_.size();) {
      if (in_flight_[i].deliver_true_us <= now_true_us_) {
        due.push_back(std::move(in_flight_[i]));
        in_flight_[i] = std::move(in_flight_.back());
        in_flight_.pop_back();
      } else {
        ++i;
      }
    }
    for (const InFlight& pkt : due) {
      Endpoint& ep = endpoints_[pkt.dest];
      if (!ep.active) {
        continue;
      }
      TxCapture tx(this, pkt.dest);
      ep.node->handle_packet(pkt.data.data(), pkt.data.size(), pkt.from,
                             ep.clk.local(now_true_us_), tx);
    }
  }

  Rng rng_;
  std::vector<Endpoint> endpoints_;
  std::vector<InFlight> in_flight_;
  int64_t now_true_us_ = 0;

  bool partitioned_ = false;
  size_t partition_boundary_ = 0;

  bool tsf_enabled_ = false;
  uint8_t tsf_bssid_[6] = {0, 0, 0, 0, 0, 0};
  int64_t tsf_offset_us_ = 0;
  int64_t tsf_interval_us_ = 500000;
  int64_t tsf_read_jitter_us_ = 25;
};

}  // namespace fakenet
