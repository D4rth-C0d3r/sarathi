#include "map_view.h"

#include <algorithm>

#include "raster.h"
#include "raw_draw.h"
#include "theme_binding.h"

namespace sarathi::ui {

void MapView::Create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
                     core::ChunkStore* store, core::MapRenderer* renderer, TimeUsFn time_us) {
  store_ = store;
  renderer_ = renderer;
  time_us_ = time_us;
  renderer_->SetClock(time_us);
  obj_ = lv_obj_create(parent);
  lv_obj_remove_style_all(obj_);
  lv_obj_set_pos(obj_, x, y);
  lv_obj_set_size(obj_, w, h);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(obj_, OnDraw, LV_EVENT_DRAW_MAIN, this);
  // The map fills its whole area, so LVGL can start a refresh here and skip everything under it
  // (the screen background: a second full-screen fill, about 15 ms in PSRAM).
  lv_obj_add_event_cb(obj_, OnCoverCheck, LV_EVENT_COVER_CHECK, this);
}

void MapView::OnCoverCheck(lv_event_t* event) {
  auto* self = static_cast<MapView*>(lv_event_get_user_data(event));
  lv_area_t coords;
  lv_obj_get_coords(self->obj_, &coords);
  if (_lv_area_is_in(lv_event_get_cover_area(event), &coords, 0)) {
    lv_event_set_cover_res(event, LV_COVER_RES_COVER);
  }
}

void MapView::Set(const core::MapCamera& camera, uint8_t level, const core::MapStyle& style) {
  camera_ = camera;
  level_ = level;
  style_ = style;
  prepared_ = false;
  lv_obj_invalidate(obj_);
}

void MapView::SetRoute(const core::RouteWindow& window, const core::TrafficState& traffic,
                       uint32_t now_ms, const core::MapRouteStyle (&styles)[core::kTrafficLevels],
                       core::Rgb maneuver_color, lv_opa_t opa) {
  route_ = core::MapRoute{};
  route_.origin_x_m = window.has_position ? window.rider_x_m : 0.0F;
  route_.origin_y_m = window.has_position ? window.rider_y_m : 0.0F;
  if (renderer_ != nullptr) {
    route_.count = core::SplitRouteByTraffic(window, traffic, now_ms, renderer_->RoutePieces(),
                                             core::kMaxTrafficPieces);
  }
  std::copy(std::begin(styles), std::end(styles), std::begin(route_.styles));
  const core::ThemeMapStyle& map = theme::Current().map;
  route_.casing_color = theme::Pixel(map.ground);
  route_.casing_px = map.route_casing_px;
  has_maneuver_ = window.has_maneuver_point;
  maneuver_ = window.maneuver_point;
  maneuver_color_ = maneuver_color;
  opa_ = opa;
  prepared_ = false;
  lv_obj_invalidate(obj_);
}

// The maneuver dot and the rider's chevron, queued as LVGL drawing over the map's pixels.
void MapView::DrawMarkers(lv_layer_t* layer, const lv_area_t& obj_area) const {
  constexpr int32_t kManeuverRadius = 10;
  constexpr lv_value_precise_t kChevronHalfWidth = 13;
  constexpr lv_value_precise_t kChevronTip = 18;
  constexpr lv_value_precise_t kChevronTail = 12;
  core::MapCamera camera = camera_;
  camera.anchor_x += obj_area.x1;
  camera.anchor_y += obj_area.y1;
  if (route_.count > 0 && has_maneuver_) {
    const core::ScreenPoint m =
        core::ProjectMetres(camera, route_.origin_x_m + static_cast<float>(maneuver_.x_m),
                            route_.origin_y_m + static_cast<float>(maneuver_.y_m));
    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.bg_color = theme::Color(maneuver_color_);
    dot.bg_opa = opa_;
    dot.radius = LV_RADIUS_CIRCLE;
    const lv_area_t area{m.x - kManeuverRadius, m.y - kManeuverRadius, m.x + kManeuverRadius,
                         m.y + kManeuverRadius};
    lv_draw_rect(layer, &dot, &area);
  }
  if (has_rider_) {
    lv_draw_triangle_dsc_t chevron;
    lv_draw_triangle_dsc_init(&chevron);
    chevron.bg_color = theme::Color(rider_color_);
    chevron.bg_opa = LV_OPA_COVER;
    const auto rx = static_cast<lv_value_precise_t>(camera.anchor_x);
    const auto ry = static_cast<lv_value_precise_t>(camera.anchor_y);
    chevron.p[0] = {rx, ry - kChevronTip};
    chevron.p[1] = {rx - kChevronHalfWidth, ry + kChevronTail};
    chevron.p[2] = {rx + kChevronHalfWidth, ry + kChevronTail};
    lv_draw_triangle(layer, &chevron);
  }
}

void MapView::SetVisible(bool visible) {
  if (visible) {
    lv_obj_remove_flag(obj_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(obj_, LV_OBJ_FLAG_HIDDEN);
  }
}

MapView::Stats MapView::TakeStats() {
  Stats s = stats_;
  s.draw_avg_us = s.draws == 0 ? 0 : static_cast<uint32_t>(draw_total_us_ / s.draws);
  s.last = renderer_->TakeStats();
  stats_ = Stats{};
  draw_total_us_ = 0;
  return s;
}

void MapView::OnDraw(lv_event_t* event) {
  auto* self = static_cast<MapView*>(lv_event_get_user_data(event));
  lv_layer_t* layer = lv_event_get_layer(event);
  if (self->renderer_ == nullptr || layer->draw_buf == nullptr ||
      layer->color_format != LV_COLOR_FORMAT_RGB565) {
    return;
  }
  const uint32_t start = self->time_us_ != nullptr ? self->time_us_() : 0;
  FinishQueued(lv_obj_get_display(self->obj_), layer);
  self->stats_.queued_us = self->time_us_ != nullptr ? self->time_us_() - start : 0;

  // Draw only inside both the area being refreshed and this object.
  lv_area_t obj_area;
  lv_obj_get_coords(self->obj_, &obj_area);
  core::Canvas canvas;
  if (!LayerCanvas(layer, obj_area, &canvas)) {
    return;
  }
  if (!self->prepared_) {
    // The first strip of a frame projects it; every strip then draws its part.
    // The camera is in this object's coordinates; the canvas in the screen's.
    core::MapCamera camera = self->camera_;
    camera.anchor_x += obj_area.x1;
    camera.anchor_y += obj_area.y1;
    camera.view_cx += obj_area.x1;
    camera.view_cy += obj_area.y1;
    self->renderer_->SetRoute(self->route_);
    self->renderer_->Prepare(self->store_, self->level_, camera, self->style_, ++self->frame_);
    self->prepared_ = true;
  }
  self->renderer_->Draw(canvas);
  self->DrawMarkers(layer, obj_area);
  if (self->time_us_ != nullptr) {
    const uint32_t took = self->time_us_() - start;
    self->draw_total_us_ += took;
    self->stats_.draw_max_us = std::max(self->stats_.draw_max_us, took);
  }
  ++self->stats_.draws;
}

}  // namespace sarathi::ui
