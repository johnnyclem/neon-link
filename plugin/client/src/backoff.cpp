#include "neon/client/backoff.hpp"

namespace neon::client {

int Backoff::next_delay_ms() {
  static constexpr int kMs[] = {250, 500, 1000, 2000, 5000};
  const int i = attempt_ < 5 ? attempt_ : 4;
  ++attempt_;
  return kMs[i];
}

void Backoff::reset() { attempt_ = 0; }

}  // namespace neon::client
