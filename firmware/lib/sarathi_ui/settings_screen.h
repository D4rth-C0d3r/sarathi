// Settings: theme, brightness, day/night, units, screen timeout, knob direction, Wi-Fi setup,
// software update, and the input test. Locked while moving: the controls are hidden and can't
// be focused.

#pragma once

#include <lvgl.h>

#include "app_model.h"
#include "wifi_setup.h"

namespace sarathi::ui::settings_screen {

// What the rows that open other screens do (screen_manager supplies them).
struct Actions {
  void (*open_input_test)();
  void (*start_wifi_setup)();
  void (*check_for_update)();
  void (*forget_wifi)();
};

lv_obj_t* Create(lv_group_t* group, core::AppModel& model, const Actions& actions);
void Update(const core::AppState& state, bool locked, const core::WifiSetup& wifi);

}  // namespace sarathi::ui::settings_screen
