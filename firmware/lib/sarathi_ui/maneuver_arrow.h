// Semantic component: the next maneuver as a vector arrow (core::GlyphFor), drawn in the
// theme's maneuver colour and stroke width. Or, when the phone sends its own icon (Chronos,
// ADR-0005), that picture scaled by whole pixels, in the same colour.

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "app_state.h"

namespace sarathi::ui {

class ManeuverArrow {
 public:
  void Create(lv_obj_t* parent, int32_t size, int32_t x_offset, int32_t y_offset);
  void Set(const core::Maneuver& maneuver);
  // A valid icon replaces the arrow; an invalid one brings the arrow back.
  void SetIcon(const core::ManeuverIcon& icon);
  void SetVisible(bool visible);

 private:
  static void OnDraw(lv_event_t* event);
  void DrawIcon(lv_layer_t* layer, const lv_area_t& area) const;

  lv_obj_t* obj_ = nullptr;
  int32_t size_ = 0;
  core::Maneuver maneuver_{};
  bool has_icon_ = false;
  uint32_t icon_id_ = 0;
  const core::ManeuverIcon* icon_ = nullptr;  // the model's; read only while drawing
};

}  // namespace sarathi::ui
