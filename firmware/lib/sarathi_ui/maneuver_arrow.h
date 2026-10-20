// Semantic component: the next maneuver as a vector arrow (core::GlyphFor), drawn in the
// theme's maneuver colour and stroke width.

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "app_state.h"

namespace sarathi::ui {

class ManeuverArrow {
 public:
  void Create(lv_obj_t* parent, int32_t size, int32_t x_offset, int32_t y_offset);
  void Set(const core::Maneuver& maneuver);

 private:
  static void OnDraw(lv_event_t* event);

  lv_obj_t* obj_ = nullptr;
  int32_t size_ = 0;
  core::Maneuver maneuver_{};
};

}  // namespace sarathi::ui
