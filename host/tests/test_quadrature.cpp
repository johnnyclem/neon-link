#include <doctest.h>

#include "neon/input/quadrature.hpp"

TEST_CASE("gray_step: legal neighbors") {
  CHECK(neon::gray_step(0b00, 0b01) == +1);
  CHECK(neon::gray_step(0b01, 0b11) == +1);
  CHECK(neon::gray_step(0b11, 0b10) == +1);
  CHECK(neon::gray_step(0b10, 0b00) == +1);

  CHECK(neon::gray_step(0b00, 0b10) == -1);
  CHECK(neon::gray_step(0b10, 0b11) == -1);
  CHECK(neon::gray_step(0b11, 0b01) == -1);
  CHECK(neon::gray_step(0b01, 0b00) == -1);
}

TEST_CASE("gray_step: hold and illegal skip are zero") {
  CHECK(neon::gray_step(0b00, 0b00) == 0);
  CHECK(neon::gray_step(0b11, 0b11) == 0);
  CHECK(neon::gray_step(0b00, 0b11) == 0);
  CHECK(neon::gray_step(0b11, 0b00) == 0);
}

TEST_CASE("QuadDecoder: one CW detent is four steps") {
  neon::QuadDecoder q;
  q.reset(0b00);
  CHECK(q.feed(0b01) == 0);
  CHECK(q.feed(0b11) == 0);
  CHECK(q.feed(0b10) == 0);
  CHECK(q.feed(0b00) == 1);
}

TEST_CASE("QuadDecoder: one CCW detent is four steps") {
  neon::QuadDecoder q;
  q.reset(0b00);
  CHECK(q.feed(0b10) == 0);
  CHECK(q.feed(0b11) == 0);
  CHECK(q.feed(0b01) == 0);
  CHECK(q.feed(0b00) == -1);
}

TEST_CASE("QuadDecoder: bounce cancels") {
  neon::QuadDecoder q;
  q.reset(0b00);
  CHECK(q.feed(0b01) == 0);
  CHECK(q.feed(0b00) == 0);
  CHECK(q.last_ab() == 0b00);
}

TEST_CASE("QuadDecoder: two CW detents") {
  neon::QuadDecoder q;
  q.reset(0b00);
  const unsigned cw[] = {0b01, 0b11, 0b10, 0b00, 0b01, 0b11, 0b10, 0b00};
  int sum = 0;
  for (unsigned ab : cw) {
    sum += q.feed(ab);
  }
  CHECK(sum == 2);
}
