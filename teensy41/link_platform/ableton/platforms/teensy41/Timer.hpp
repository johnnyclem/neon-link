// Polled timer with the AsioTimer surface Link's protocol code uses:
// expires_at / expires_from_now / async_wait / cancel / now. A handler
// is never invoked after cancel() or after the timer is destroyed
// (matching upstream's SafeAsyncHandler guarantee); re-arming while a
// wait is pending simply moves the deadline.

#pragma once

#include <ableton/platforms/teensy41/Runtime.hpp>

#include <chrono>
#include <memory>
#include <utility>

namespace ableton
{
namespace platforms
{
namespace teensy41
{

class PolledTimer
{
public:
  using ErrorCode = ::neonnet::error_code;
  // system_clock::time_point like AsioTimer: PeerGateway names that type
  // in its timeout list. The values carried are our monotonic t41 clock,
  // re-based into system_clock's duration — system_clock::now() itself
  // is never consulted.
  using TimePoint = std::chrono::system_clock::time_point;

  PolledTimer()
    : mpState(Runtime::instance().makeTimer())
  {
  }

  PolledTimer(const PolledTimer&) = delete;
  PolledTimer& operator=(const PolledTimer&) = delete;

  PolledTimer(PolledTimer&& rhs)
    : mpState(std::move(rhs.mpState))
  {
  }

  ~PolledTimer()
  {
    if (mpState != nullptr)
    {
      cancel();
    }
  }

  void expires_at(TimePoint tp)
  {
    mpState->dueUs =
      std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch())
        .count();
  }

  template <typename Duration>
  void expires_from_now(Duration duration)
  {
    mpState->dueUs =
      t41_now_us()
      + std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
  }

  void cancel()
  {
    mpState->armed = false;
    mpState->handler = nullptr;
  }

  template <typename Handler>
  void async_wait(Handler handler)
  {
    mpState->handler = [handler](::neonnet::error_code e) mutable { handler(e); };
    mpState->armed = true;
  }

  TimePoint now() const
  {
    return TimePoint{std::chrono::duration_cast<std::chrono::system_clock::duration>(
      std::chrono::microseconds{t41_now_us()})};
  }

private:
  std::shared_ptr<TimerState> mpState;
};

} // namespace teensy41
} // namespace platforms
} // namespace ableton
