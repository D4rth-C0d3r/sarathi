#include "maneuver_arrow.h"

#include <algorithm>
#include <cmath>

#include "maneuver_glyph.h"
#include "maneuver_icon.h"
#include "theme_binding.h"
#include "visibility.h"

namespace sarathi::ui {

void ManeuverArrow::Create(lv_obj_t* parent, int32_t size, int32_t x_offset, int32_t y_offset) {
  size_ = size;
  obj_ = lv_obj_create(parent);
  lv_obj_remove_style_all(obj_);
  lv_obj_set_size(obj_, size, size);
  lv_obj_align(obj_, LV_ALIGN_CENTER, x_offset, y_offset);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(obj_, OnDraw, LV_EVENT_DRAW_MAIN, this);
}

void ManeuverArrow::Set(const core::Maneuver& maneuver) {
  if (maneuver != maneuver_) {
    maneuver_ = maneuver;
    lv_obj_invalidate(obj_);
  }
}

void ManeuverArrow::SetIcon(const core::ManeuverIcon& icon) {
  icon_ = &icon;
  if (icon.valid != has_icon_ || (icon.valid && icon.id != icon_id_)) {
    has_icon_ = icon.valid;
    icon_id_ = icon.id;
    lv_obj_invalidate(obj_);
  }
}

void ManeuverArrow::SetVisible(bool visible) { ui::SetVisible(obj_, visible); }

// One filled rectangle per run of set pixels, centred in the object.
void ManeuverArrow::DrawIcon(lv_layer_t* layer, const lv_area_t& area) const {
  const int32_t scale = std::max<int32_t>(1, size_ / static_cast<int32_t>(core::kManeuverIconSize));
  const int32_t inset = (size_ - (scale * static_cast<int32_t>(core::kManeuverIconSize))) / 2;
  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  rect.bg_color = theme::Color(theme::Current().palette.maneuver);
  rect.bg_opa = LV_OPA_COVER;
  core::ForEachIconRun(*icon_, [&](const core::IconRun& run) {
    lv_area_t a;
    a.x1 = area.x1 + inset + (run.x * scale);
    a.y1 = area.y1 + inset + (run.y * scale);
    a.x2 = a.x1 + (run.length * scale) - 1;
    a.y2 = a.y1 + scale - 1;
    lv_draw_rect(layer, &rect, &a);
  });
}

void ManeuverArrow::OnDraw(lv_event_t* event) {
  const auto* self = static_cast<const ManeuverArrow*>(lv_event_get_user_data(event));
  if (self->has_icon_ && self->icon_ != nullptr) {
    lv_area_t area;
    lv_obj_get_coords(self->obj_, &area);
    self->DrawIcon(lv_event_get_layer(event), area);
    return;
  }
  const core::ManeuverGlyph glyph = core::GlyphFor(self->maneuver_);
  if (glyph.stroke_count == 0) {
    return;
  }
  lv_layer_t* layer = lv_event_get_layer(event);
  lv_area_t area;
  lv_obj_get_coords(self->obj_, &area);

  const core::ThemeDescriptor& t = theme::Current();
  const float scale = static_cast<float>(self->size_) / static_cast<float>(core::kGlyphSize);
  const auto width = static_cast<int32_t>(static_cast<float>(t.components.arrow_stroke_px) * scale);
  // lv_value_precise_t is an integer unless LV_USE_FLOAT is set, so round explicitly.
  auto x = [&](int v) {
    return static_cast<lv_value_precise_t>(
        std::lround(static_cast<float>(area.x1) + (static_cast<float>(v) * scale)));
  };
  auto y = [&](int v) {
    return static_cast<lv_value_precise_t>(
        std::lround(static_cast<float>(area.y1) + (static_cast<float>(v) * scale)));
  };

  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.color = theme::Color(t.palette.maneuver);
  line.width = width;
  line.round_start = 1;
  line.round_end = 1;
  for (size_t s = 0; s < glyph.stroke_count; ++s) {
    const core::GlyphStroke& stroke = glyph.strokes[s];
    for (size_t i = 0; i + 1 < stroke.count; ++i) {
      line.p1 = {x(stroke.points[i].x), y(stroke.points[i].y)};
      line.p2 = {x(stroke.points[i + 1].x), y(stroke.points[i + 1].y)};
      lv_draw_line(layer, &line);
    }
  }

  if (glyph.has_ring) {
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = theme::Color(t.palette.maneuver);
    arc.width = width;
    arc.start_angle = 0;
    arc.end_angle = 360;
    arc.center = {static_cast<int32_t>(x(glyph.ring_center.x)),
                  static_cast<int32_t>(y(glyph.ring_center.y))};
    arc.radius = static_cast<uint16_t>(static_cast<float>(glyph.ring_radius) * scale);
    lv_draw_arc(layer, &arc);
  }
}

}  // namespace sarathi::ui
