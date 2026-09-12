#include <doctest.h>

#include "neon/osc/scene.hpp"

using neon::osc::SceneOp;
using neon::osc::scene_next;
using neon::osc::scene_prev;

TEST_CASE("scene_next fires selected until the last row") {
  CHECK(scene_next(0, 14).op == SceneOp::kFireSelected);
  CHECK(scene_next(12, 14).op == SceneOp::kFireSelected);
  CHECK(scene_next(13, 14).op == SceneOp::kStop);
  CHECK(scene_next(20, 14).op == SceneOp::kStop);
}

TEST_CASE("scene_prev fires the previous index and stops on the first") {
  auto p = scene_prev(5, 14);
  CHECK(p.op == SceneOp::kFireIndex);
  CHECK(p.index == 4);
  CHECK(scene_prev(0, 14).op == SceneOp::kStop);
  CHECK(scene_prev(-1, 14).op == SceneOp::kStop);
}

TEST_CASE("prev from past the last row fires the last scene") {
  // next-beyond-last parks selected == count (one past the end).
  auto p = scene_prev(14, 14);
  CHECK(p.op == SceneOp::kFireIndex);
  CHECK(p.index == 13);
  CHECK(scene_next(14, 14).op == SceneOp::kStop);
}

TEST_CASE("empty set stops on prev and is optimistic on next") {
  CHECK(scene_prev(0, 0).op == SceneOp::kStop);
  CHECK(scene_next(0, 0).op == SceneOp::kFireSelected);
}
