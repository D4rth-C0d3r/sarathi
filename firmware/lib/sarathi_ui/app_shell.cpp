#include "app_shell.h"

#include <lvgl.h>

#include "button_debouncer.h"
#include "input.h"
#include "placeholder_style.h"
#include "screen_manager.h"

namespace sarathi::ui::app_shell {
namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
core::AppModel* app = nullptr;
core::InputTestModel* test = nullptr;
core::ButtonDebouncer button(0, 0, kLongPressMs);  // timings set in Init()
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

bool InputTestOpen() { return screen_manager::Current() == screen_manager::ScreenId::kInputTest; }

}  // namespace

void Init(core::AppModel& model, core::InputTestModel& input_test, uint32_t debounce_ms,
          uint32_t double_click_ms) {
  app = &model;
  test = &input_test;
  button = core::ButtonDebouncer(debounce_ms, double_click_ms, kLongPressMs);

  // LVGL's default theme in dark mode for widget basics (rollers, sliders); our screens set
  // their own colours and fonts on top.
  lv_display_t* display = lv_display_get_default();
  lv_theme_t* theme =
      lv_theme_default_init(display, lv_color_hex(style::kAccent), lv_color_hex(style::kRejectFill),
                            true, style::StoppedOnly());
  lv_display_set_theme(display, theme);

  screen_manager::Init(input::Init(), model);
}

void OnKnobStep(int8_t step) {
  if (InputTestOpen()) {
    test->OnKnobStep(step);
  } else {
    input::AddSteps(step);
  }
}

void OnButtonSample(bool raw_pressed, uint32_t now_ms) {
  const bool input_test_open = InputTestOpen();
  if (input_test_open) {
    test->OnButtonSample(raw_pressed, now_ms);
  }
  switch (button.Update(raw_pressed, now_ms).gesture) {
    case core::ButtonGesture::kClick:
      if (!input_test_open) {
        input::Click();
      }
      break;
    case core::ButtonGesture::kDoubleClick:
      if (!input_test_open) {
        screen_manager::NextScreen(*app);
      }
      break;
    case core::ButtonGesture::kLongPress:
      if (input_test_open) {
        screen_manager::CloseInputTest();
      }
      break;
    case core::ButtonGesture::kNone:
      break;
  }
}

void OnTouch(bool touching, int16_t x, int16_t y) { test->OnTouch(touching, x, y); }

void Update(uint32_t now_ms) { screen_manager::Update(*app, test->State(), now_ms); }

int BacklightPercent() {
  return InputTestOpen() ? test->State().backlight_percent
                         : app->State().settings.brightness_percent;
}

}  // namespace sarathi::ui::app_shell
