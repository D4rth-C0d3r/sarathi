#include "map_view.h"

#include <algorithm>

#include "raster.h"

namespace sarathi::ui {

namespace {

// LVGL without an OS draws a layer's tasks inline as they're dispatched. A bound in case one
// never finishes.
constexpr int kMaxDispatch = 1000;

void FinishQueued(lv_display_t* display, lv_layer_t* layer) {
  for (int i = 0; i < kMaxDispatch && layer->draw_task_head != nullptr; ++i) {
    lv_draw_dispatch_layer(display, layer);
  }
}

}  // namespace

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
  if (layer->draw_buf == nullptr || layer->color_format != LV_COLOR_FORMAT_RGB565) {
    return;
  }
  const uint32_t start = self->time_us_ != nullptr ? self->time_us_() : 0;
  FinishQueued(lv_obj_get_display(self->obj_), layer);
  self->stats_.queued_us = self->time_us_ != nullptr ? self->time_us_() - start : 0;

  // Draw only inside both the area being refreshed and this object.
  lv_area_t obj_area;
  lv_obj_get_coords(self->obj_, &obj_area);
  lv_area_t clip;
  if (!_lv_area_intersect(&clip, &layer->_clip_area, &obj_area)) {
    return;
  }
  core::Canvas canvas;
  // RGB565 draw buffer, 2-byte aligned by LVGL.
  canvas.px = reinterpret_cast<uint16_t*>(layer->draw_buf->data);  // NOLINT(*-reinterpret-cast)
  canvas.stride = static_cast<int32_t>(layer->draw_buf->header.stride / sizeof(uint16_t));
  canvas.buf_x = layer->buf_area.x1;
  canvas.buf_y = layer->buf_area.y1;
  canvas.clip = core::ClipRect{clip.x1, clip.y1, clip.x2, clip.y2};
  if (!self->prepared_) {
    // The first strip of a frame projects it; every strip then draws its part.
    // The camera is in this object's coordinates; the canvas in the screen's.
    core::MapCamera camera = self->camera_;
    camera.anchor_x += obj_area.x1;
    camera.anchor_y += obj_area.y1;
    camera.view_cx += obj_area.x1;
    camera.view_cy += obj_area.y1;
    self->renderer_->Prepare(*self->store_, self->level_, camera, self->style_, ++self->frame_);
    self->prepared_ = true;
  }
  self->renderer_->Draw(canvas);
  if (self->time_us_ != nullptr) {
    const uint32_t took = self->time_us_() - start;
    self->draw_total_us_ += took;
    self->stats_.draw_max_us = std::max(self->stats_.draw_max_us, took);
  }
  ++self->stats_.draws;
}

}  // namespace sarathi::ui
