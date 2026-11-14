// Semantic component: a ring around the screen edge showing a fraction (progress to the next
// turn, track position). Track and indicator follow the theme. Drawn with core's rasteriser
// straight into LVGL's buffer (step G6.2): LVGL's arc cost about 20 ms a frame over the moving
// map, every strip touching it.

#pragma once

#include <cstdint>

#include <lvgl.h>

namespace sarathi::ui {

class RingGauge {
 public:
  void Create(lv_obj_t* parent, int32_t diameter);

  // 0.0 (empty) to 1.0 (full).
  void Set(float fraction);

  // Shows or hides the ring; only changes (and redraws) when the visibility changes.
  void SetVisible(bool visible);

 private:
  static void OnDraw(lv_event_t* event);
  // Redraws the part of the ring between two steps (either order), round ends included.
  void InvalidateSteps(int32_t a, int32_t b) const;

  // 50 steps is plenty for a glance and keeps redraws rare (on Nav: one step per 10 m, like the
  // distance label's rounding).
  static constexpr int32_t kRange = 50;
  lv_obj_t* obj_ = nullptr;
  int32_t diameter_ = 0;
  int32_t shown_ = 0;
  bool visible_ = true;
};

}  // namespace sarathi::ui
