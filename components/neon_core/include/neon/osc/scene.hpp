#pragma once

#include <cstdint>

namespace neon {
namespace osc {

// Session-grid walk for AbletonOSC. selected and count are 0-based
// Live indices (scene 1 on the glass is selected==0).
enum class SceneOp : uint8_t { kFireSelected = 0, kFireIndex = 1, kStop = 2 };

struct SceneDecision {
  SceneOp op = SceneOp::kFireSelected;
  int index = 0;
};

inline SceneDecision scene_next(int selected, int count) {
  SceneDecision d{};
  if (count <= 0) {
    d.op = SceneOp::kFireSelected;
    return d;
  }
  if (selected < 0) {
    d.op = SceneOp::kFireSelected;
    return d;
  }
  if (selected >= count - 1) {
    d.op = SceneOp::kStop;
    return d;
  }
  d.op = SceneOp::kFireSelected;
  return d;
}

inline SceneDecision scene_prev(int selected, int count) {
  SceneDecision d{};
  if (count <= 0 || selected <= 0) {
    d.op = SceneOp::kStop;
    return d;
  }
  d.op = SceneOp::kFireIndex;
  d.index = selected - 1;
  return d;
}

}  // namespace osc
}  // namespace neon
