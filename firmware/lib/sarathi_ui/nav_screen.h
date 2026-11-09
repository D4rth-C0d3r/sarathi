// Nav placeholder: next maneuver and distance (primary), street and ETA (supporting), and the
// route window's status until the map renderer exists (ADR-0002). Nothing to focus.

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "app_state.h"
#include "chunk_store.h"
#include "map_render.h"

namespace sarathi::ui::nav_screen {

// The map's chunks (null: no map source, only the route draws) and the renderer, allocated by
// the platform (about 330 KB; PSRAM on the device). Before Create().
// `time_us` (optional) times the map's drawing for the perf log.
void SetMapData(core::ChunkStore* store, core::MapRenderer* renderer,
                uint32_t (*time_us)() = nullptr);

// The map's drawing since the last call, for the perf log: per frame, in microseconds.
struct MapPerf {
  uint32_t frames = 0;
  uint32_t total_us = 0;  // MapView's own draw, all strips
  uint32_t prepare_us = 0;
  uint32_t lines_us = 0;
};
MapPerf TakeMapPerf();

lv_obj_t* Create();
void Update(const core::AppState& state, uint32_t now_ms);

}  // namespace sarathi::ui::nav_screen
