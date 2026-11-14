// Drawing straight into LVGL's draw buffer with core's rasteriser (the map, the ring gauge):
// far faster than LVGL's own lines and arcs on this panel (ADR-0009). Call from an object's
// LV_EVENT_DRAW_MAIN handler. UI task only.

#pragma once

#include <lvgl.h>

#include "raster.h"

namespace sarathi::ui {

// Lets LVGL finish what it has already queued for the layer (objects behind this one), so the
// pixels written next go on top. LVGL without an OS draws the tasks inline.
void FinishQueued(lv_display_t* display, lv_layer_t* layer);

// The layer's buffer as a canvas clipped to both the area being refreshed and `obj_area`. False
// when they don't meet, or the buffer isn't RGB565.
bool LayerCanvas(const lv_layer_t* layer, const lv_area_t& obj_area, core::Canvas* canvas);

}  // namespace sarathi::ui
