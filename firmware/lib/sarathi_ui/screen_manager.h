// Owns the screens and their focus groups, decides which one is shown, and points the knob
// (LVGL encoder) at the shown screen's group.
//
//   Nav -> Music -> Settings -> Nav ...   double-click; Settings is skipped while moving
//   Call                                  shown while a call rings or is active, then back
//   Input test                            from Settings; hold the button to leave

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "app_model.h"
#include "input_test_model.h"

namespace sarathi::ui::screen_manager {

enum class ScreenId : uint8_t { kNav, kMusic, kSettings, kCall, kInputTest };

// Builds every screen once and shows Nav.
void Init(lv_indev_t* encoder, core::AppModel& model);

// Shows the call screen when needed and refreshes the shown screen.
void Update(const core::AppModel& model, const core::InputTestState& input_test, uint32_t now_ms);

// Double-click: the next screen in the carousel. Ignored on the call and input test screens.
void NextScreen(const core::AppModel& model);

void OpenInputTest();
void CloseInputTest();

[[nodiscard]] ScreenId Current();

}  // namespace sarathi::ui::screen_manager
