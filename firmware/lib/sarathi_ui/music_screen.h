// Music placeholder: title, artist, position, and previous / play-pause / next.

#pragma once

#include <lvgl.h>

#include "app_model.h"

namespace sarathi::ui::music_screen {

lv_obj_t* Create(lv_group_t* group, core::AppModel& model);
void Update(const core::AppState& state);

}  // namespace sarathi::ui::music_screen
