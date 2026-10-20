// Settings placeholder: theme (stored only, until the theme system), brightness, and the
// input test. Locked while moving: the controls are hidden and can't be focused.

#pragma once

#include <lvgl.h>

#include "app_model.h"

namespace sarathi::ui::settings_screen {

using OpenInputTestFn = void (*)();

lv_obj_t* Create(lv_group_t* group, core::AppModel& model, OpenInputTestFn open_input_test);
void Update(const core::AppState& state, bool locked);

}  // namespace sarathi::ui::settings_screen
