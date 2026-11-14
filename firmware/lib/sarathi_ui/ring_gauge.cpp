#include "ring_gauge.h"

#include <algorithm>
#include <cmath>

#include "raster.h"
#include "raw_draw.h"
#include "theme_binding.h"

namespace sarathi::ui {

void RingGauge::Create(lv_obj_t* parent, int32_t diameter) {
  diameter_ = diameter;
  shown_ = 0;
  visible_ = true;
  obj_ = lv_obj_create(parent);
  lv_obj_remove_style_all(obj_);
  lv_obj_set_size(obj_, diameter, diameter);
  lv_obj_center(obj_);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(obj_, OnDraw, LV_EVENT_DRAW_MAIN, this);
}

void RingGauge::Set(float fraction) {
  const auto value = static_cast<int32_t>(std::lround(std::clamp(fraction, 0.0F, 1.0F) * kRange));
  if (value != shown_) {
    InvalidateSteps(shown_, value);
    shown_ = value;
  }
}

void RingGauge::SetVisible(bool visible) {
  if (visible != visible_) {
    visible_ = visible;
    if (visible) {
      lv_obj_remove_flag(obj_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(obj_, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void RingGauge::InvalidateSteps(int32_t a, int32_t b) const {
  constexpr float kTwoPi = 6.28318531F;
  const float r_out = static_cast<float>(diameter_) / 2;
  const float r_in = r_out - static_cast<float>(theme::Current().components.ring_width_px);
  const float from = kTwoPi * static_cast<float>(std::min(a, b)) / kRange;
  const float to = kTwoPi * static_cast<float>(std::max(a, b)) / kRange;
  // The box around both ends at both radii, and the outermost point of each quarter crossed.
  float min_x = r_out;
  float max_x = -r_out;
  float min_y = r_out;
  float max_y = -r_out;
  const auto add = [&](float angle, float r) {
    const float x = std::sin(angle) * r;
    const float y = -std::cos(angle) * r;
    min_x = std::min(min_x, x);
    max_x = std::max(max_x, x);
    min_y = std::min(min_y, y);
    max_y = std::max(max_y, y);
  };
  for (const float angle : {from, to}) {
    add(angle, r_out);
    add(angle, r_in);
  }
  for (int q = 1; q <= 3; ++q) {
    const float angle = kTwoPi * static_cast<float>(q) / 4;
    if (angle > from && angle < to) {
      add(angle, r_out);
    }
  }
  // The round ends reach half the width past each end; a pixel more for rounding.
  const auto pad = static_cast<int32_t>(std::ceil((r_out - r_in) / 2)) + 2;
  lv_area_t coords;
  lv_obj_get_coords(obj_, &coords);
  const float cx = static_cast<float>(coords.x1) + r_out;
  const float cy = static_cast<float>(coords.y1) + r_out;
  const lv_area_t area{static_cast<int32_t>(std::floor(cx + min_x)) - pad,
                       static_cast<int32_t>(std::floor(cy + min_y)) - pad,
                       static_cast<int32_t>(std::ceil(cx + max_x)) + pad,
                       static_cast<int32_t>(std::ceil(cy + max_y)) + pad};
  lv_obj_invalidate_area(obj_, &area);
}

void RingGauge::OnDraw(lv_event_t* event) {
  auto* self = static_cast<RingGauge*>(lv_event_get_user_data(event));
  lv_layer_t* layer = lv_event_get_layer(event);
  lv_area_t obj_area;
  lv_obj_get_coords(self->obj_, &obj_area);
  FinishQueued(lv_obj_get_display(self->obj_), layer);
  core::Canvas canvas;
  if (!LayerCanvas(layer, obj_area, &canvas)) {
    return;
  }
  const core::ThemeDescriptor& t = theme::Current();
  core::Ring ring;
  // Pixel-centre coordinates: the object's pixels x1..x2 cover x1 to x2 + 1.
  ring.cx = static_cast<float>(obj_area.x1) + static_cast<float>(self->diameter_) / 2;
  ring.cy = static_cast<float>(obj_area.y1) + static_cast<float>(self->diameter_) / 2;
  ring.r_out = static_cast<float>(self->diameter_) / 2;
  ring.width = static_cast<float>(t.components.ring_width_px);
  ring.track = theme::Pixel(t.palette.surface);
  ring.indicator = theme::Pixel(t.palette.accent);
  ring.fraction = static_cast<float>(self->shown_) / kRange;
  core::DrawRing(canvas, ring);
}

}  // namespace sarathi::ui
