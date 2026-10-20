// The UI shell: turns knob, button and touch input into LVGL focus/clicks and screen changes,
// and keeps the screens in sync with the app model. Shared by the device and the simulator;
// their mains only feed it input and call Update().
//
//   Rotate        move focus; change a value in edit mode (slider, roller)
//   Click         activate the focused item (sent after the 300 ms double-click window)
//   Double-click  next screen
//   Long press    leave the input test screen
//
// On the input test screen, rotation, clicks and touch go to the input test model instead.

#pragma once

#include <cstdint>

#include "app_model.h"
#include "input_test_model.h"

namespace sarathi::ui::app_shell {

// How long the button must be held for a long press.
constexpr uint32_t kLongPressMs = 800;

// Builds the input device and screens. Call once after LVGL and a display exist. The button
// timings come from the board (board_pins.h).
void Init(core::AppModel& model, core::InputTestModel& input_test, uint32_t debounce_ms,
          uint32_t double_click_ms);

void OnKnobStep(int8_t step);
void OnButtonSample(bool raw_pressed, uint32_t now_ms);
void OnTouch(bool touching, int16_t x, int16_t y);

// Refreshes the screens from the model. Call after lv_timer_handler().
void Update(uint32_t now_ms);

// The backlight to apply now: the input test's own level while it is open, otherwise the
// brightness setting.
[[nodiscard]] int BacklightPercent();

}  // namespace sarathi::ui::app_shell
