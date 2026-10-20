#include "route_view.h"

#include "layout.h"
#include "theme_binding.h"

namespace sarathi::ui {
namespace {

constexpr int32_t kManeuverRadius = 10;
constexpr lv_value_precise_t kChevronHalfWidth = 13;
constexpr lv_value_precise_t kChevronTip = 18;
constexpr lv_value_precise_t kChevronTail = 12;

}  // namespace

void RouteView::Create(lv_obj_t* parent, int32_t top, const core::ProjectionParams& params) {
  params_ = params;
  obj_ = lv_obj_create(parent);
  lv_obj_remove_style_all(obj_);
  lv_obj_set_pos(obj_, 0, top);
  lv_obj_set_size(obj_, layout::kScreenSize, layout::kScreenSize - top);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(obj_, OnDraw, LV_EVENT_DRAW_MAIN, this);
}

void RouteView::Set(const core::RouteWindow& window, bool stale) {
  if (has_route_ && window.sequence == sequence_ && stale == stale_) {
    return;
  }
  core::ProjectRoute(window, params_, projected_);
  sequence_ = window.sequence;
  stale_ = stale;
  has_route_ = true;
  lv_obj_invalidate(obj_);
}

void RouteView::Clear() {
  if (has_route_) {
    has_route_ = false;
    projected_.count = 0;
    lv_obj_invalidate(obj_);
  }
}

void RouteView::OnDraw(lv_event_t* event) {
  const auto* self = static_cast<const RouteView*>(lv_event_get_user_data(event));
  lv_layer_t* layer = lv_event_get_layer(event);
  const core::ThemeDescriptor& t = theme::Current();
  const lv_opa_t opa = self->stale_ ? LV_OPA_40 : LV_OPA_COVER;

  // Coordinates are screen coordinates; the screen sits at (0, 0).
  if (self->has_route_) {
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.round_start = 1;
    line.round_end = 1;
    line.opa = opa;
    for (size_t i = 0; i < self->projected_.count; ++i) {
      const core::ScreenSegment& s = self->projected_.segments[i];
      line.color = theme::Color(s.behind ? t.palette.route_behind : t.palette.route_line);
      line.width = s.behind ? t.components.route_behind_px : t.components.route_line_px;
      line.p1 = {static_cast<lv_value_precise_t>(s.a.x), static_cast<lv_value_precise_t>(s.a.y)};
      line.p2 = {static_cast<lv_value_precise_t>(s.b.x), static_cast<lv_value_precise_t>(s.b.y)};
      lv_draw_line(layer, &line);
    }

    if (self->projected_.has_maneuver) {
      lv_draw_rect_dsc_t dot;
      lv_draw_rect_dsc_init(&dot);
      dot.bg_color = theme::Color(t.palette.maneuver);
      dot.bg_opa = opa;
      dot.radius = LV_RADIUS_CIRCLE;
      const core::ScreenPoint m = self->projected_.maneuver;
      const lv_area_t area{m.x - kManeuverRadius, m.y - kManeuverRadius, m.x + kManeuverRadius,
                           m.y + kManeuverRadius};
      lv_draw_rect(layer, &dot, &area);
    }
  }

  // The rider: a chevron pointing up (the direction of travel).
  lv_draw_triangle_dsc_t chevron;
  lv_draw_triangle_dsc_init(&chevron);
  chevron.bg_color = theme::Color(t.palette.rider);
  chevron.bg_opa = LV_OPA_COVER;
  const auto rx = static_cast<lv_value_precise_t>(self->params_.rider.x);
  const auto ry = static_cast<lv_value_precise_t>(self->params_.rider.y);
  chevron.p[0] = {rx, ry - kChevronTip};
  chevron.p[1] = {rx - kChevronHalfWidth, ry + kChevronTail};
  chevron.p[2] = {rx + kChevronHalfWidth, ry + kChevronTail};
  lv_draw_triangle(layer, &chevron);
}

}  // namespace sarathi::ui
