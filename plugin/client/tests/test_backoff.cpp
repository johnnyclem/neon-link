#include <doctest.h>

#include "neon/client/backoff.hpp"
#include "neon/client/ratelimit.hpp"

TEST_CASE("backoff 250 500 1000 2000 5000 cap") {
  neon::client::Backoff b;
  CHECK(b.next_delay_ms() == 250);
  CHECK(b.next_delay_ms() == 500);
  CHECK(b.next_delay_ms() == 1000);
  CHECK(b.next_delay_ms() == 2000);
  CHECK(b.next_delay_ms() == 5000);
  CHECK(b.next_delay_ms() == 5000);
  b.reset();
  CHECK(b.next_delay_ms() == 250);
}

TEST_CASE("coalesce fires immediately then waits") {
  neon::client::Coalesce c(100);
  CHECK(c.note(0));
  CHECK_FALSE(c.note(50));
  CHECK_FALSE(c.note(99));
  CHECK(c.note(100));
  CHECK_FALSE(c.note(150));
  CHECK(c.note(200));
}
