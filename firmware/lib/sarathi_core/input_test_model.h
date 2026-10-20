// State behind the Phase 0/1 input test screen: knob count, button, touch and backlight.
// Pure logic shared by the device and the simulator, so both behave the same.

#pragma once

#include <cstdint>

#include "button_debouncer.h"

namespace sarathi::core {

// Backlight change per knob detent, as in Elecrow's Lesson02-Rotate.
constexpr int kBacklightStepPercent = 5;

struct InputTestState {
  int knob_count = 0;
  bool button_pressed = false;
  ButtonGesture last_gesture = ButtonGesture::kNone;
  bool touching = false;
  int16_t touch_x = 0;
  int16_t touch_y = 0;
  int backlight_percent = 0;
};

class InputTestModel {
 public:
  InputTestModel(uint32_t debounce_ms, uint32_t double_click_ms, int initial_backlight_percent,
                 int backlight_step_percent);

  // One knob detent (+1 or -1). Moves the count and the backlight (clamped to 5-100 %).
  void OnKnobStep(int8_t step);

  // Raw button level, sampled every few ms.
  void OnButtonSample(bool raw_pressed, uint32_t now_ms);

  // Touch position in screen coordinates while touching.
  void OnTouch(bool touching, int16_t x, int16_t y);

  [[nodiscard]] const InputTestState& State() const { return state_; }

 private:
  ButtonDebouncer button_;
  int backlight_step_percent_;
  InputTestState state_;
};

}  // namespace sarathi::core
