#include "raw_draw.h"

#include <cstdint>

namespace sarathi::ui {

namespace {

// A bound in case a task never finishes.
constexpr int kMaxDispatch = 1000;

}  // namespace

void FinishQueued(lv_display_t* display, lv_layer_t* layer) {
  for (int i = 0; i < kMaxDispatch && layer->draw_task_head != nullptr; ++i) {
    lv_draw_dispatch_layer(display, layer);
  }
}

bool LayerCanvas(const lv_layer_t* layer, const lv_area_t& obj_area, core::Canvas* canvas) {
  if (layer->draw_buf == nullptr || layer->color_format != LV_COLOR_FORMAT_RGB565) {
    return false;
  }
  lv_area_t clip;
  if (!_lv_area_intersect(&clip, &layer->_clip_area, &obj_area)) {
    return false;
  }
  // RGB565 draw buffer, 2-byte aligned by LVGL.
  canvas->px = reinterpret_cast<uint16_t*>(layer->draw_buf->data);  // NOLINT(*-reinterpret-cast)
  canvas->stride = static_cast<int32_t>(layer->draw_buf->header.stride / sizeof(uint16_t));
  canvas->buf_x = layer->buf_area.x1;
  canvas->buf_y = layer->buf_area.y1;
  canvas->clip = core::ClipRect{clip.x1, clip.y1, clip.x2, clip.y2};
  return true;
}

}  // namespace sarathi::ui
