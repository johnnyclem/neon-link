#pragma once

#include <cstdint>

namespace neon {

// Packed-mask decoder for an 8-key pad (M5Stack Unit ByteButton U192 and
// anything else that reports "bit N = 1 means button N is down"). Polarity
// and I2C live in the caller; this only turns a stable mask into shorts,
// holds, auto-repeat, and two-key combos.
class ByteButtonDecoder {
 public:
  static constexpr int kCount = 8;
  static constexpr int64_t kDebounceUs = 20000;
  static constexpr int64_t kHoldUs = 500000;
  static constexpr int64_t kRepeatUs = 90000;

  struct Event {
    enum class Kind : uint8_t {
      kNone = 0,
      kPress = 1,  // rose, not a combo
      kShort = 2,  // fell before hold, not swallowed
      kHold = 3,
      kRepeat = 4,
      kCombo = 5,  // a < b
    };
    Kind kind = Kind::kNone;
    uint8_t a = 0;
    uint8_t b = 0;
  };

  void reset() { *this = ByteButtonDecoder{}; }

  // First sample is idle — no events. Later samples debounce, then emit.
  void feed(uint8_t pressed, int64_t now_us) {
    if (!primed_) {
      stable_ = pressed;
      candidate_ = pressed;
      cand_us_ = now_us;
      primed_ = true;
      for (int i = 0; i < kCount; ++i) {
        pad_[i].down = (pressed & (1u << i)) != 0;
        pad_[i].down_us = now_us;
      }
      return;
    }

    if (pressed != candidate_) {
      candidate_ = pressed;
      cand_us_ = now_us;
    } else if (pressed != stable_ && now_us - cand_us_ >= kDebounceUs) {
      apply_stable(pressed, now_us);
    }

    for (int i = 0; i < kCount; ++i) {
      Pad& p = pad_[i];
      if (!p.down || p.swallowed) {
        continue;
      }
      if (!p.hold_fired && now_us - p.down_us >= kHoldUs) {
        p.hold_fired = true;
        p.repeat_us = now_us;
        push({Event::Kind::kHold, static_cast<uint8_t>(i), 0});
      } else if (p.hold_fired && now_us - p.repeat_us >= kRepeatUs) {
        p.repeat_us = now_us;
        push({Event::Kind::kRepeat, static_cast<uint8_t>(i), 0});
      }
    }
  }

  Event take() {
    if (q_len_ == 0) {
      return {};
    }
    Event e = q_[q_head_];
    q_head_ = static_cast<uint8_t>((q_head_ + 1) % kQ);
    --q_len_;
    return e;
  }

  uint8_t down_mask() const { return stable_; }

 private:
  static constexpr int kQ = 8;

  struct Pad {
    bool down = false;
    bool hold_fired = false;
    bool swallowed = false;
    int64_t down_us = 0;
    int64_t repeat_us = 0;
  };

  void apply_stable(uint8_t pressed, int64_t now_us) {
    const uint8_t rose = static_cast<uint8_t>(pressed & ~stable_);
    const uint8_t fell = static_cast<uint8_t>(stable_ & ~pressed);

    for (int i = 0; i < kCount; ++i) {
      if ((rose & (1u << i)) == 0) {
        continue;
      }
      Pad& p = pad_[i];
      p.down = true;
      p.hold_fired = false;
      p.swallowed = false;
      p.down_us = now_us;
      p.repeat_us = now_us;

      int other = -1;
      for (int j = 0; j < kCount; ++j) {
        if (j != i && pad_[j].down && !pad_[j].swallowed) {
          other = j;
          break;
        }
      }
      if (other >= 0) {
        pad_[other].swallowed = true;
        p.swallowed = true;
        const uint8_t lo = static_cast<uint8_t>(other < i ? other : i);
        const uint8_t hi = static_cast<uint8_t>(other < i ? i : other);
        push({Event::Kind::kCombo, lo, hi});
      } else {
        push({Event::Kind::kPress, static_cast<uint8_t>(i), 0});
      }
    }

    for (int i = 0; i < kCount; ++i) {
      if ((fell & (1u << i)) == 0) {
        continue;
      }
      Pad& p = pad_[i];
      const bool short_ok = p.down && !p.hold_fired && !p.swallowed;
      p.down = false;
      p.hold_fired = false;
      p.swallowed = false;
      if (short_ok) {
        push({Event::Kind::kShort, static_cast<uint8_t>(i), 0});
      }
    }
    stable_ = pressed;
  }

  void push(Event e) {
    if (q_len_ >= kQ) {
      return;
    }
    const uint8_t i = static_cast<uint8_t>((q_head_ + q_len_) % kQ);
    q_[i] = e;
    ++q_len_;
  }

  bool primed_ = false;
  uint8_t stable_ = 0;
  uint8_t candidate_ = 0;
  int64_t cand_us_ = 0;
  Pad pad_[kCount] = {};
  Event q_[kQ] = {};
  uint8_t q_head_ = 0;
  uint8_t q_len_ = 0;
};

}  // namespace neon
