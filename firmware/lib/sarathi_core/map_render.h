// The on-device map's drawing (ADR-0009 "Device: drawing" and "Performance plan"): stored
// chunks projected heading-up around the rider and rasterised. Culling by the round screen's
// circle, first per chunk, then per line and polygon box; one fixed-point (Q16) affine transform
// per chunk; points that land on the same pixel are skipped. Pure logic, so it runs and is timed
// natively; sarathi_ui only hands it LVGL's draw buffer.

#pragma once

#include <cstddef>
#include <cstdint>

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

struct MapRenderStats {
  uint32_t chunks = 0;    // chunks drawn
  uint32_t shapes = 0;    // lines and polygons drawn
  uint32_t segments = 0;  // segments rasterised
};

namespace detail {

// screen = b + p.x * a_x + p.y * a_y, Q16, for points in one chunk's quanta.
struct Affine {
  int32_t bx = 0;
  int32_t axx = 0;
  int32_t axy = 0;
  int32_t by = 0;
  int32_t ayx = 0;
  int32_t ayy = 0;

  [[nodiscard]] ScreenPoint Apply(const MapPoint& p) const {
    constexpr int32_t kHalf = 1 << 15;
    return ScreenPoint{(bx + p.x * axx + p.y * axy + kHalf) >> 16,
                       (by + p.x * ayx + p.y * ayy + kHalf) >> 16};
  }
};

// The visible circle in one chunk's quanta, for box tests in integers.
struct QuantaCircle {
  int32_t cx = 0;
  int32_t cy = 0;
  int64_t r2 = 0;
};

struct ChunkView {
  StoredChunk chunk;
  Affine affine;
  QuantaCircle view;
};

}  // namespace detail

// Scratch for drawing, kept out of the UI task's small stack: allocate one at init.
class MapRenderer {
 public:
  static constexpr size_t kMaxVisible = 48;  // chunks drawn in one frame
  static constexpr size_t kMaxRing = 1024;   // protocol: points per polygon

  // Draws the background, then polygons, then roads from minor to major, of the stored chunks
  // at `level` that the camera sees, inside the canvas's clip. Marks the drawn chunks with
  // `frame` for eviction.
  MapRenderStats Render(ChunkStore& store, uint8_t level, const MapCamera& camera,
                        const MapStyle& style, Canvas& canvas, uint32_t frame);

 private:
  detail::ChunkView views_[kMaxVisible];
  ScreenPoint ring_[kMaxRing];
};

// Screen position of a ride-frame point (metres), for tests and overlays.
[[nodiscard]] ScreenPoint ProjectMetres(const MapCamera& camera, float x_m, float y_m);

}  // namespace sarathi::core
