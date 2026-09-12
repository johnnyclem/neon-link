#include <doctest.h>

#include "neon/input/bytebutton.hpp"

using neon::ByteButtonDecoder;
using Kind = ByteButtonDecoder::Event::Kind;

namespace {
ByteButtonDecoder::Event drain_one(ByteButtonDecoder& d) { return d.take(); }

int drain_count(ByteButtonDecoder& d) {
  int n = 0;
  while (d.take().kind != Kind::kNone) {
    ++n;
  }
  return n;
}
}  // namespace

TEST_CASE("ByteButtonDecoder: first sample is idle") {
  ByteButtonDecoder d;
  d.feed(0x01, 0);
  CHECK(d.take().kind == Kind::kNone);
  CHECK(d.down_mask() == 0x01);
}

TEST_CASE("ByteButtonDecoder: bounce shorter than debounce is ignored") {
  ByteButtonDecoder d;
  d.feed(0, 0);
  d.feed(0x01, 1000);
  d.feed(0, 5000);
  CHECK(d.take().kind == Kind::kNone);
}

TEST_CASE("ByteButtonDecoder: stable press and release is press then short") {
  ByteButtonDecoder d;
  d.feed(0, 0);
  d.feed(0x01, 1000);
  d.feed(0x01, 1000 + ByteButtonDecoder::kDebounceUs);
  auto press = drain_one(d);
  CHECK(press.kind == Kind::kPress);
  CHECK(press.a == 0);
  d.feed(0, 80000);
  d.feed(0, 80000 + ByteButtonDecoder::kDebounceUs);
  const auto e = d.take();
  CHECK(e.kind == Kind::kShort);
  CHECK(e.a == 0);
  CHECK(d.take().kind == Kind::kNone);
}

TEST_CASE("ByteButtonDecoder: hold fires once then repeats") {
  ByteButtonDecoder d;
  d.feed(0, 0);
  d.feed(0x04, 1000);  // B2
  const int64_t down = 1000 + ByteButtonDecoder::kDebounceUs;
  d.feed(0x04, down);
  auto press = drain_one(d);
  CHECK(press.kind == Kind::kPress);
  CHECK(press.a == 2);
  CHECK(drain_count(d) == 0);

  d.feed(0x04, down + ByteButtonDecoder::kHoldUs);
  auto hold = drain_one(d);
  CHECK(hold.kind == Kind::kHold);
  CHECK(hold.a == 2);

  d.feed(0x04, down + ByteButtonDecoder::kHoldUs +
                   ByteButtonDecoder::kRepeatUs);
  auto rep = drain_one(d);
  CHECK(rep.kind == Kind::kRepeat);
  CHECK(rep.a == 2);
}

TEST_CASE("ByteButtonDecoder: combo swallows both shorts") {
  ByteButtonDecoder d;
  d.feed(0, 0);
  d.feed(0x01, 1000);
  const int64_t t0 = 1000 + ByteButtonDecoder::kDebounceUs;
  d.feed(0x01, t0);
  CHECK(drain_one(d).kind == Kind::kPress);
  d.feed(0x03, t0 + 30000);
  const int64_t t1 = t0 + 30000 + ByteButtonDecoder::kDebounceUs;
  d.feed(0x03, t1);
  auto combo = drain_one(d);
  CHECK(combo.kind == Kind::kCombo);
  CHECK(combo.a == 0);
  CHECK(combo.b == 1);

  d.feed(0, t1 + 40000);
  d.feed(0, t1 + 40000 + ByteButtonDecoder::kDebounceUs);
  CHECK(d.take().kind == Kind::kNone);
}

TEST_CASE("ByteButtonDecoder: hold is not also a short") {
  ByteButtonDecoder d;
  d.feed(0, 0);
  d.feed(0x01, 1000);
  const int64_t down = 1000 + ByteButtonDecoder::kDebounceUs;
  d.feed(0x01, down);
  CHECK(drain_one(d).kind == Kind::kPress);
  d.feed(0x01, down + ByteButtonDecoder::kHoldUs);
  CHECK(drain_one(d).kind == Kind::kHold);
  d.feed(0, down + ByteButtonDecoder::kHoldUs + 40000);
  d.feed(0, down + ByteButtonDecoder::kHoldUs + 40000 +
                ByteButtonDecoder::kDebounceUs);
  CHECK(d.take().kind == Kind::kNone);
}
