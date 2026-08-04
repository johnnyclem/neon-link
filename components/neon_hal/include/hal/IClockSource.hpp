#pragma once

#include <cstdint>

namespace hal {

// Monotonic microsecond timebase. On the ESP32-S3 this is esp_timer, which
// is also what Ableton Link's ESP32 platform clock uses — the engine, Link,
// and the pulse hardware all share one time domain.
class IClockSource {
 public:
  virtual ~IClockSource() = default;
  virtual int64_t now_us() const = 0;
};

}  // namespace hal
