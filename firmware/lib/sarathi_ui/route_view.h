// Semantic component: the route window drawn heading-up (ADR-0002) with the next maneuver
// marked and the rider chevron fixed. Geometry comes from core::ProjectRoute; colours and line
// widths from the theme. Redraws only when the window or staleness changes.

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "app_state.h"
#include "route_projection.h"

namespace sarathi::ui {

class RouteView {
 public:
  // Draws only inside the rectangle from `top` to the bottom of the screen; projection is in
  // screen coordinates.
  void Create(lv_obj_t* parent, int32_t top, const core::ProjectionParams& params);

  // `stale` draws the route dimmed; `metres_per_px` is the zoom. Re-projects only when the
  // window or staleness changes (a zoom change waits for the next window), unless `force`
  // (used by the stress build).
  void Set(const core::RouteWindow& window, bool stale, float metres_per_px, bool force = false);
  void Clear();

 private:
  static void OnDraw(lv_event_t* event);

  lv_obj_t* obj_ = nullptr;
  core::ProjectionParams params_{};
  core::ProjectedRoute projected_{};
  uint32_t sequence_ = 0;
  bool has_route_ = false;
  bool stale_ = false;
};

}  // namespace sarathi::ui
