#include "ring_gauge.h"

#include <algorithm>
#include <cmath>

#include "theme_binding.h"

namespace sarathi::ui {

void RingGauge::Create(lv_obj_t* parent, int32_t diameter) {
  arc_ = lv_arc_create(parent);
  lv_obj_set_size(arc_, diameter, diameter);
  lv_obj_center(arc_);
  lv_arc_set_rotation(arc_, 270);  // start at the top, fill clockwise
  lv_arc_set_bg_angles(arc_, 0, 360);
  lv_arc_set_range(arc_, 0, kRange);
  lv_arc_set_value(arc_, 0);
  lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_style(arc_, theme::GaugeTrackStyle(), LV_PART_MAIN);
  lv_obj_add_style(arc_, theme::GaugeIndicatorStyle(), LV_PART_INDICATOR);
}

void RingGauge::Set(float fraction) {
  const auto value = static_cast<int32_t>(std::lround(std::clamp(fraction, 0.0F, 1.0F) * kRange));
  if (value != shown_) {
    lv_arc_set_value(arc_, value);
    shown_ = value;
  }
}

}  // namespace sarathi::ui
