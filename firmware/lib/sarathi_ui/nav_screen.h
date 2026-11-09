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
void SetMapData(core::ChunkStore* store, core::MapRenderer* renderer);

lv_obj_t* Create();
void Update(const core::AppState& state, uint32_t now_ms);

}  // namespace sarathi::ui::nav_screen
