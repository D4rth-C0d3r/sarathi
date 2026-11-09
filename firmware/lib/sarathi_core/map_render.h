// The on-device map's drawing (ADR-0009 "Device: drawing" and "Performance plan"): stored
// chunks projected heading-up around the rider and rasterised. Pure logic, so it runs and is
// timed natively; sarathi_ui only hands it LVGL's draw buffer.
//
// Two steps, because LVGL renders a frame in strips (step G1 bench: small strips in internal RAM
// are far faster than full-screen PSRAM buffers):
// - Prepare(), once per frame: cull chunks and shapes by the round screen, project every
//   visible point once (one fixed-point transform per chunk, dropping points that land on the
//   previous pixel) into a screen-space cache with a box per shape.
// - Draw(), per strip: the background, then the cached shapes whose box crosses the strip:
//   polygons, then roads from minor to major (glow first, when the style has one).

#pragma once

#include <cstddef>
#include <cstdint>

#include "app_state.h"
#include "chunk_store.h"
#include "raster.h"

namespace sarathi::core {

constexpr int kRoadClasses = 7;  // motorway, trunk, primary, secondary, tertiary, minor, service

// Where the map looks from: the rider at `anchor` on screen, heading up.
struct MapCamera {
  float x_m = 0;  // ride frame
  float y_m = 0;
  float heading_deg = 0;  // clockwise from north; drawn pointing up
  float metres_per_px = 2;
  int32_t anchor_x = 240;
  int32_t anchor_y = 320;
  int32_t view_cx = 240;  // the visible circle
  int32_t view_cy = 240;
  int32_t view_r = 240;
};

// Raw RGB565 colours in the panel's order.
struct MapStyle {
  uint16_t background = 0;
  uint16_t water = 0;
  uint16_t park = 0;
  bool draw_polygons = true;
  uint16_t road[kRoadClasses] = {};
  uint8_t road_px[kRoadClasses] = {};
  // A second, wider pass under each road (Racing's glow): 0 for none.
  uint8_t glow_extra_px = 0;
  uint16_t glow[kRoadClasses] = {};
  // Service roads aren't drawn above this zoom (ADR-0009).
  float service_max_metres_per_px = 3;
};

// The route, drawn over the roads: points relative to `origin` (a route window and where the
// rider was when it was cut, in the ride frame).
struct MapRoute {
  float origin_x_m = 0;
  float origin_y_m = 0;
  const RoutePoint* points = nullptr;
  size_t count = 0;
  uint16_t color = 0;
  uint8_t width_px = 0;
};

struct MapRenderStats {
  uint32_t chunks = 0;     // chunks seen by Prepare()
  uint32_t shapes = 0;     // lines and polygons cached
  uint32_t segments = 0;   // segments cached
  bool truncated = false;  // the cache filled up: the least important shapes were left out
  // Time per part, with a clock set (MapRenderer::SetClock); 0 otherwise. They add up over
  // frames and strips until TakeStats().
  uint32_t prepare_us = 0;
  uint32_t background_us = 0;
  uint32_t polygons_us = 0;
  uint32_t lines_us = 0;
};

// The cache and scratch, kept out of the UI task's small stack: allocate one at init (about
// 330 KB; PSRAM on the device).
class MapRenderer {
 public:
  static constexpr size_t kMaxVisibleChunks = 48;
  static constexpr size_t kMaxShapes = 4096;
  static constexpr size_t kMaxPoints = 32768;

  // Projects what the camera sees of the stored chunks at `level`. Marks those chunks drawn
  // with `frame` for eviction. Call once per frame, before Draw().
  // `store` may be null (no map source): then only the route draws. Level 0 also draws level-2
  // chunks, the quarters of dense L0 tiles (protocol 1.2).
  void Prepare(ChunkStore* store, uint8_t level, const MapCamera& camera, const MapStyle& style,
               uint32_t frame);
  // The route for the next Prepare(); count 0 for none.
  void SetRoute(const MapRoute& route) { route_ = route; }
  // Draws the prepared frame inside the canvas's clip.
  void Draw(Canvas& canvas);
  // Both, for a single canvas.
  MapRenderStats Render(ChunkStore& store, uint8_t level, const MapCamera& camera,
                        const MapStyle& style, Canvas& canvas, uint32_t frame);

  // Stats of the prepared frame, and of the drawing since the last call.
  MapRenderStats TakeStats();

  // A microsecond clock to time the parts of a frame (the bench); none by default.
  void SetClock(uint32_t (*time_us)()) { time_us_ = time_us; }

 private:
  struct Shape {
    int32_t min_x, min_y, max_x, max_y;  // screen box of the projected points
    uint32_t first;                      // into points_
    uint16_t count;
    uint8_t kind;  // road class, or polygon kind
    bool polygon;
  };
  struct Seen;

  uint32_t (*time_us_)() = nullptr;
  MapStyle style_{};
  MapRenderStats stats_{};
  Shape shapes_[kMaxShapes] = {};
  size_t n_shapes_ = 0;
  ScreenPoint points_[kMaxPoints] = {};
  size_t n_points_ = 0;
  MapRoute route_{};
  ScreenPoint route_points_[kMaxRoutePoints] = {};
  size_t n_route_ = 0;
};

// Screen position of a ride-frame point (metres), for tests and overlays.
[[nodiscard]] ScreenPoint ProjectMetres(const MapCamera& camera, float x_m, float y_m);

}  // namespace sarathi::core
