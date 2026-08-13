#pragma once

namespace neon::client {

// Reconnect delay: 250, 500, 1000, 2000, then 5000 ms cap.
class Backoff {
 public:
  int next_delay_ms();
  void reset();
  int attempt() const { return attempt_; }

 private:
  int attempt_ = 0;
};

}  // namespace neon::client
