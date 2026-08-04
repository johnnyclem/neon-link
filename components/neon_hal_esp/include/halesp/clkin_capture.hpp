#pragma once

#include <cstdint>

namespace halesp {

// Edge kind delivered by the capture queue.
enum class CaptureKind : uint8_t { kClock = 0, kReset = 1 };

struct CaptureEvent {
  CaptureKind kind;
  int64_t t_us;  // esp_timer domain — same timebase as Link and the engine
};

// Installs IRAM rising-edge ISRs on CLK IN and RST IN. Timestamps go to a
// small queue drained by the Link service task.
bool clkin_capture_init(int clk_gpio, int rst_gpio);

// Non-blocking pop; returns false when the queue is empty.
bool clkin_capture_pop(CaptureEvent* out);

}  // namespace halesp
