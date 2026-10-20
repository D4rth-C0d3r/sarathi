// Nav placeholder: next maneuver and distance (primary), street and ETA (supporting), and the
// route window's status until the map renderer exists (ADR-0002). Nothing to focus.

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "app_state.h"

namespace sarathi::ui::nav_screen {

lv_obj_t* Create();
void Update(const core::AppState& state, uint32_t now_ms);

}  // namespace sarathi::ui::nav_screen
