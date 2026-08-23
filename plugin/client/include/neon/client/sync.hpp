#pragma once

// The plugin's Neon Sync peer: socket/thread glue around the portable
// nsync::Node + nsync::DawFollower, the desktop sibling of
// components/neon_sync_esp. This is what takes the module ecosystem
// beyond the REST protocol inside the DAW — the plugin joins the mesh
// itself (discovery, announces, ping/pong clock measurement, LWW state)
// instead of poking tempo over HTTP.
//
// No JUCE. POSIX sockets only (the plugin ships on macOS; the host test
// graph compiles this on Linux with the same warnings and sanitizers).
//
// Threading:
//  - audio thread: publish_playhead() only — wait-free seqlock write.
//  - service thread (owned here): socket pump, follower update, node poll.
//  - message thread: start()/stop()/set_drive()/status(), mutex-guarded.
//
// One instance per machine is the intended shape (the plugin is a
// set-wide device manager). Multiple instances still bind via
// SO_REUSEPORT, but unicast measurement replies then land on one of the
// sockets arbitrarily, so extra instances may stay on coarse offsets.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

#include "nsync/daw_follower.hpp"
#include "nsync/node.hpp"
#include "nsync/playhead_mailbox.hpp"

namespace neon::client {

struct SyncStatus : nsync::FollowerStatus {
  bool running = false;
  uint64_t node_id = 0;
};

class SyncService {
 public:
  SyncService();
  ~SyncService();
  SyncService(const SyncService&) = delete;
  SyncService& operator=(const SyncService&) = delete;

  // The local microsecond clock every playhead sample must be stamped
  // with (steady, audio-thread safe).
  static int64_t now_us();

  // Joins the mesh: brings the node up and starts the socket thread.
  // Idempotent. No sockets or threads exist before the first call, so
  // constructing the service is plugin-scan-safe.
  void start(double initial_bpm = 120.0);

  // Announces BYE, closes the socket, joins the thread. Idempotent; the
  // service can be started again afterwards.
  void stop();

  bool running() const { return running_.load(std::memory_order_acquire); }

  // Drive = the DAW writes the mesh while its transport runs
  // (docs/NEON_SYNC.md §7.4). Off = monitor-only peer.
  void set_drive(bool enabled);
  bool drive() const;

  // Audio thread. Wait-free; latest sample wins.
  void publish_playhead(const nsync::DawPlayhead& ph) noexcept {
    mailbox_.publish(ph);
  }

  SyncStatus status() const;

 private:
  void loop();
  bool open_socket();
  void join_group();
  void close_socket();

  mutable std::mutex mu_;  // guards node_ + follower_
  uint64_t node_id_ = 0;
  nsync::Node node_;
  nsync::DawFollower follower_;
  nsync::PlayheadMailbox mailbox_;

  std::thread thread_;
  std::atomic<bool> stop_flag_{false};
  std::atomic<bool> running_{false};

  // Service thread only.
  int sock_ = -1;
  uint32_t group_be_ = 0;
  int64_t last_join_us_ = 0;
};

}  // namespace neon::client
