// Phase 0 input check: knob count, button state, touch position and backlight level.
// Replaced by the UI shell in Phase 1.

#pragma once

#include "input_test_model.h"

namespace sarathi::ui::input_test_screen {

using State = core::InputTestState;

// Builds the screen and makes it active. Call once after LVGL and a display exist.
void Create();

// Refreshes only the parts that changed. Call from the UI task.
void Update(const State& state);

}  // namespace sarathi::ui::input_test_screen
