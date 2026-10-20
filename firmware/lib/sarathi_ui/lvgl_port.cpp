// Display and input registration follow Elecrow's demo (vendor/crowpanel-2.1/example/Arduino/
// RotaryScreen_2_1/RotaryScreen_2_1.ino, setup()): two full-screen RGB565 buffers in PSRAM,
// partial render mode, and a pointer input device.

#include "lvgl_port.h"

#include <cstddef>

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <lvgl.h>

namespace sarathi::ui::lvgl_port {
namespace {

constexpr const char* kTag = "lvgl";

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
FlushFn flush_fn = nullptr;
TouchFn touch_fn = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

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

uint32_t TickMs() { return millis(); }

}  // namespace

bool Init(int16_t width, int16_t height, FlushFn flush, TouchFn touch) {
  flush_fn = flush;
  touch_fn = touch;

  lv_init();
  lv_tick_set_cb(TickMs);

  const size_t bytes = sizeof(uint16_t) * static_cast<size_t>(width) * static_cast<size_t>(height);
  void* buf1 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  void* buf2 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (buf1 == nullptr || buf2 == nullptr) {
    ESP_LOGE(kTag, "draw buffer alloc failed (2 x %u B in PSRAM)", static_cast<unsigned>(bytes));
    heap_caps_free(buf1);
    heap_caps_free(buf2);
    return false;
  }

  lv_display_t* display = lv_display_create(width, height);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, FlushCb);
  lv_display_set_buffers(display, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, TouchCb);
  lv_indev_set_display(indev, display);

  ESP_LOGI(kTag, "ready (2 x %u B buffers in PSRAM)", static_cast<unsigned>(bytes));
  return true;
}

void Run() { lv_timer_handler(); }

}  // namespace sarathi::ui::lvgl_port
