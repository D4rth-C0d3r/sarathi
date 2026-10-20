// Semantic component: a ring around the screen edge showing a fraction (progress to the next
// turn, track position). Track and indicator follow the theme.

#pragma once

#include <cstdint>

#include <lvgl.h>

namespace sarathi::ui {

class RingGauge {
 public:
  void Create(lv_obj_t* parent, int32_t diameter);

  // 0.0 (empty) to 1.0 (full).
  void Set(float fraction);

 private:
  // 50 steps is plenty for a glance and keeps redraws rare: every change repaints the whole
  // ring (on Nav: one step per 10 m, like the distance label's rounding).
  static constexpr int32_t kRange = 50;
  lv_obj_t* arc_ = nullptr;
  int32_t shown_ = -1;
};

}  // namespace sarathi::ui
