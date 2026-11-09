// Semantic component: the on-device map (ADR-0009). Draws stored chunks with core::MapRenderer
// straight into LVGL's draw buffer: LVGL's own line drawing is far too slow for a map (the
// spike measured 92 ms against 19 ms). It first lets LVGL finish what is already queued for the
// layer (the screen background), then writes its pixels; objects drawn after it (labels, the
// rider arrow) still go on top. Colours come in as raw pixels (theme::Pixel). UI task only.

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "chunk_store.h"
#include "map_render.h"

namespace sarathi::ui {

class MapView {
 public:
  using TimeUsFn = uint32_t (*)();

  // `store` and `renderer` outlive the view. `time_us` (optional) times each draw.
  void Create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, core::ChunkStore* store,
              core::MapRenderer* renderer, TimeUsFn time_us = nullptr);

  // Redraws with this camera, level and style.
  void Set(const core::MapCamera& camera, uint8_t level, const core::MapStyle& style);
  void SetVisible(bool visible);

  struct Stats {
    uint32_t draws = 0;
    uint32_t draw_avg_us = 0;
    uint32_t draw_max_us = 0;
    core::MapRenderStats last;
  };
  // Since the last call.
  Stats TakeStats();

 private:
  static void OnDraw(lv_event_t* event);

  lv_obj_t* obj_ = nullptr;
  core::ChunkStore* store_ = nullptr;
  core::MapRenderer* renderer_ = nullptr;
  TimeUsFn time_us_ = nullptr;
  core::MapCamera camera_{};
  core::MapStyle style_{};
  uint8_t level_ = 0;
  uint32_t frame_ = 0;
  uint64_t draw_total_us_ = 0;
  Stats stats_{};
};

}  // namespace sarathi::ui
