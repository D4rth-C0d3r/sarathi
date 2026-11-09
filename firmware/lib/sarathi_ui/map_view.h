// Semantic component: the on-device map (ADR-0009). Draws stored chunks with core::MapRenderer
// straight into LVGL's draw buffer: LVGL's own line drawing is far too slow for a map (the
// spike measured 92 ms against 19 ms). It first lets LVGL finish what is already queued for the
// layer (the screen background), then writes its pixels; objects drawn after it (labels, the
// rider arrow) still go on top. Colours come in as raw pixels (theme::Pixel). UI task only.

#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include "chunk_store.h"
#include "map_render.h"
#include "theme.h"

namespace sarathi::ui {

class MapView {
 public:
  using TimeUsFn = uint32_t (*)();

  // `store` and `renderer` outlive the view. `time_us` (optional) times each draw.
  void Create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, core::ChunkStore* store,
              core::MapRenderer* renderer, TimeUsFn time_us = nullptr);

  // Redraws with this camera, level and style. `store` (in Create) may be null: no map source.
  void Set(const core::MapCamera& camera, uint8_t level, const core::MapStyle& style);

  // The route window (points relative to the camera's position) drawn over the roads, and the
  // next maneuver's dot; colours are raw pixels or theme colours. Count 0: no route.
  void SetRoute(const core::RouteWindow& window, uint16_t route_pixel, uint8_t width_px,
                core::Rgb maneuver_color, lv_opa_t opa);
  // The rider's chevron at the camera's anchor, pointing up.
  void SetRider(core::Rgb color) {
    rider_color_ = color;
    has_rider_ = true;
  }
  void SetVisible(bool visible);

  struct Stats {
    uint32_t draws = 0;
    uint32_t draw_avg_us = 0;
    uint32_t draw_max_us = 0;
    uint32_t queued_us = 0;     // LVGL's queued drawing finished first, last draw
    core::MapRenderStats last;  // the last prepared frame; draw parts add up since last call
  };
  // Since the last call.
  Stats TakeStats();

 private:
  static void OnDraw(lv_event_t* event);
  static void OnCoverCheck(lv_event_t* event);
  void DrawMarkers(lv_layer_t* layer, const lv_area_t& obj_area) const;

  lv_obj_t* obj_ = nullptr;
  core::ChunkStore* store_ = nullptr;
  core::MapRenderer* renderer_ = nullptr;
  TimeUsFn time_us_ = nullptr;
  core::MapCamera camera_{};
  core::MapStyle style_{};
  std::array<core::RoutePoint, core::kMaxRoutePoints> route_points_{};
  core::MapRoute route_{};
  bool has_maneuver_ = false;
  core::RoutePoint maneuver_{};
  core::Rgb maneuver_color_ = 0;
  lv_opa_t opa_ = LV_OPA_COVER;
  bool has_rider_ = false;
  core::Rgb rider_color_ = 0;
  uint8_t level_ = 0;
  uint32_t frame_ = 0;
  bool prepared_ = false;  // the renderer holds this camera's frame
  uint64_t draw_total_us_ = 0;
  Stats stats_{};
};

}  // namespace sarathi::ui
