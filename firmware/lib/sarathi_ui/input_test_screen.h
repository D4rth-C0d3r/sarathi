// Phase 0 input check: knob count, button state, touch position and backlight level.
// Replaced by the UI shell in Phase 1.

#pragma once

#include <cstdint>

#include "button_debouncer.h"

namespace sarathi::ui::input_test_screen {

struct State {
  int knob_count = 0;
  bool button_pressed = false;
  core::ButtonGesture last_gesture = core::ButtonGesture::kNone;
  bool touching = false;
  int16_t touch_x = 0;
  int16_t touch_y = 0;
  int backlight_percent = 0;
};

// Builds the screen and makes it active. Call once after lvgl_port::Init().
void Create();

// Refreshes only the parts that changed. Call from the UI task.
void Update(const State& state);

}  // namespace sarathi::ui::input_test_screen
