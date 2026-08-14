// Callback dispatcher for the single-threaded platform. Upstream
// variants debounce callbacks onto a worker thread; here invoke() runs
// the callback synchronously on the main thread — the caller *is* the
// thread the callback must run on, and the callbacks (peer count,
// tempo, start/stop) are cheap.

#pragma once

#include <utility>

namespace ableton
{
namespace platforms
{
namespace teensy41
{

template <typename Callback, typename Duration>
class SyncCallbackDispatcher
{
public:
  SyncCallbackDispatcher(Callback callback, Duration /*fallbackPeriod*/)
    : mCallback(std::move(callback))
  {
  }

  void start() {}
  void stop() {}
  void invoke() { mCallback(); }

private:
  Callback mCallback;
};

} // namespace teensy41
} // namespace platforms
} // namespace ableton
