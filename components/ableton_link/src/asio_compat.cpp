// asio-on-ESP-IDF compatibility, mirroring Espressif's official asio port
// (esp-protocols/components/asio). Built only on the real-Link leg.

#include <climits>
#include <unistd.h>

#include "asio/detail/config.hpp"
#include "asio/detail/posix_event.hpp"
#include "asio/detail/throw_error.hpp"
#include "asio/error.hpp"

namespace asio {
namespace detail {

// Replaces asio's posix_event constructor (suppressed via the
// ASIO_DETAIL_IMPL_POSIX_EVENT_IPP guard define): the stock version runs
// pthread_condattr_init/setclock/destroy, which return ENOSYS on ESP-IDF
// and would make asio throw at startup. A plain pthread_cond_init works.
// Check asio's posix_event() when upgrading the Link submodule so no new
// initialization step is missed.
posix_event::posix_event() : state_(0) {
  const int error = ::pthread_cond_init(&cond_, nullptr);
  asio::error_code ec(error, asio::error::get_system_category());
  asio::detail::throw_error(ec, "event");
}

}  // namespace detail
}  // namespace asio

// Referenced by asio's POSIX codepaths; newlib on ESP-IDF has no pause().
extern "C" int pause(void) {
  for (;;) {
    ::sleep(UINT_MAX);
  }
}
