// The knob as an LVGL encoder input device, so every screen works with focus groups:
// rotation moves focus (or changes a value in edit mode), a click activates.
//
// The app shell decides what a click is (after the double-click window, so a double-click
// never also presses a button) and passes it here as a short synthetic press.

#pragma once

#include <lvgl.h>

namespace sarathi::ui::input {

// Creates the encoder input device on the default display. Call once after the display.
lv_indev_t* Init();

// Knob detents since the last read (+ clockwise).
void AddSteps(int steps);

// One click on the focused object.
void Click();

}  // namespace sarathi::ui::input
