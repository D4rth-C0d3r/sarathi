#include "input_test_model.h"

#include "backlight_level.h"

namespace sarathi::core {

InputTestModel::InputTestModel(uint32_t debounce_ms, uint32_t double_click_ms,
                               int initial_backlight_percent, int backlight_step_percent)
    : button_(debounce_ms, double_click_ms), backlight_step_percent_(backlight_step_percent) {
  state_.backlight_percent = ClampBacklightPercent(initial_backlight_percent);
}

void InputTestModel::OnKnobStep(int8_t step) {
  state_.knob_count += step;
  state_.backlight_percent =
      ClampBacklightPercent(state_.backlight_percent + (step * backlight_step_percent_));
}

void InputTestModel::OnButtonSample(bool raw_pressed, uint32_t now_ms) {
  const ButtonUpdate update = button_.Update(raw_pressed, now_ms);
  state_.button_pressed = update.pressed;
  if (update.gesture != ButtonGesture::kNone) {
    state_.last_gesture = update.gesture;
  }
}

void InputTestModel::OnTouch(bool touching, int16_t x, int16_t y) {
  state_.touching = touching;
  if (touching) {
    state_.touch_x = x;
    state_.touch_y = y;
  }
}

}  // namespace sarathi::core
