// Call placeholder: caller, Accept/Reject while ringing, End while active. Shown by the screen
// manager whenever a call is ringing or active.

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "app_model.h"

namespace sarathi::ui::call_screen {

lv_obj_t* Create(lv_group_t* group, core::AppModel& model);
void Update(const core::AppState& state, uint32_t now_ms);

}  // namespace sarathi::ui::call_screen
