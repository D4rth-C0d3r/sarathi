// Hardware input check: knob count, button state and gestures, touch position and backlight.
// Reached from Settings -> Input test; hold the button to leave. Kept so anyone can verify
// their board the same way.

#pragma once

#include <lvgl.h>

#include "input_test_model.h"

namespace sarathi::ui::input_test_screen {

using State = core::InputTestState;

// Builds the screen (not loaded). Call once after LVGL and a display exist.
lv_obj_t* Create();

// Refreshes only the parts that changed. Call from the UI task.
void Update(const State& state);

}  // namespace sarathi::ui::input_test_screen
