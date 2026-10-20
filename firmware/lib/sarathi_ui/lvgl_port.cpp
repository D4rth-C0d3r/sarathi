// Display and input registration follow Elecrow's demo (vendor/crowpanel-2.1/example/Arduino/
// RotaryScreen_2_1/RotaryScreen_2_1.ino, setup()): two full-screen RGB565 buffers, partial
// render mode, and a pointer input device.

#include "lvgl_port.h"

#include <lvgl.h>

namespace sarathi::ui::lvgl_port {
namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
FlushFn flush_fn = nullptr;
TouchFn touch_fn = nullptr;
TimeUsFn time_us_fn = nullptr;
uint32_t refresh_start_us = 0;
core::FrameStats frame_stats;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// REFR_START fires on every refresh tick; RENDER_READY only when something was drawn. So
// the pair measures real frames only (lv_refr.c, LVGL 9.1).
void OnRefreshStart(lv_event_t* /*event*/) { refresh_start_us = time_us_fn(); }
void OnRenderReady(lv_event_t* /*event*/) { frame_stats.Add(time_us_fn() - refresh_start_us); }

void FlushCb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
  flush_fn(static_cast<int16_t>(area->x1), static_cast<int16_t>(area->y1),
           static_cast<int16_t>(lv_area_get_width(area)),
           static_cast<int16_t>(lv_area_get_height(area)),
           reinterpret_cast<uint16_t*>(px_map));  // NOLINT(*-reinterpret-cast)
  lv_display_flush_ready(display);
}

void TouchCb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
  int16_t x = 0;
  int16_t y = 0;
  if (touch_fn(x, y)) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;  // LVGL keeps the last point
  }
}

}  // namespace

size_t BufferBytes(int16_t width, int16_t height) {
  return sizeof(uint16_t) * static_cast<size_t>(width) * static_cast<size_t>(height);
}

bool Init(int16_t width, int16_t height, FlushFn flush, TouchFn touch, TickFn tick, AllocFn alloc) {
  flush_fn = flush;
  touch_fn = touch;

  lv_init();
  lv_tick_set_cb(tick);

  const size_t bytes = BufferBytes(width, height);
  void* buf1 = alloc(bytes);
  void* buf2 = alloc(bytes);
  if (buf1 == nullptr || buf2 == nullptr) {
    return false;  // one-time init failure; the caller logs and disables the UI
  }

  lv_display_t* display = lv_display_create(width, height);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, FlushCb);
  lv_display_set_buffers(display, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, TouchCb);
  lv_indev_set_display(indev, display);
  return true;
}

void Run() { lv_timer_handler(); }

void AttachFrameTimer(lv_display_t* display, TimeUsFn time_us) {
  time_us_fn = time_us;
  lv_display_add_event_cb(display, OnRefreshStart, LV_EVENT_REFR_START, nullptr);
  lv_display_add_event_cb(display, OnRenderReady, LV_EVENT_RENDER_READY, nullptr);
}

core::FrameStats::Summary TakeFrameStats() { return frame_stats.TakeSummary(); }

}  // namespace sarathi::ui::lvgl_port
