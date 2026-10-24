#include "app_shell.h"

#include <lvgl.h>

#include "button_debouncer.h"
#include "display_policy.h"
#include "input.h"
#include "screen_manager.h"
#include "theme_binding.h"
#include "theme_registry.h"

namespace sarathi::ui::app_shell {
namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
core::AppModel* app = nullptr;
core::InputTestModel* test = nullptr;
core::ButtonDebouncer button(0, 0, kLongPressMs);  // timings set in Init()
core::ScreenPower power;
bool last_button_raw = false;
bool swallow_button = false;  // the press that woke the screen, until it is released
bool last_touching = false;
bool swallow_touch = false;  // the touch that woke the screen, until it lifts
lv_obj_t* notice = nullptr;
uint32_t notice_until_ms = 0;
bool notice_shown = false;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

bool InputTestOpen() { return screen_manager::Current() == screen_manager::ScreenId::kInputTest; }

constexpr int32_t kNoticeWidth = 360;

void CreateNotice() {
  notice = lv_label_create(lv_layer_top());
  lv_obj_add_style(notice, theme::CardStyle(), 0);
  lv_obj_add_style(notice, theme::TextStyle(theme::Text::kSupporting), 0);
  lv_obj_set_width(notice, kNoticeWidth);
  lv_obj_set_style_text_align(notice, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(notice, LV_LABEL_LONG_WRAP);
  lv_obj_center(notice);
  lv_obj_add_flag(notice, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

void Init(core::AppModel& model, core::InputTestModel& input_test, uint32_t debounce_ms,
          uint32_t double_click_ms, core::OtaUpdate& ota, core::WifiSetup& wifi_setup,
          uint32_t (*random)()) {
  app = &model;
  test = &input_test;
  button = core::ButtonDebouncer(debounce_ms, double_click_ms, kLongPressMs);

  theme::Apply(core::ThemeFor(model.State().settings.theme));  // before any screen exists
  screen_manager::Init(input::Init(), model, ota, wifi_setup, random);
  CreateNotice();
}

void ShowNotice(const char* text, uint32_t duration_ms) {
  lv_label_set_text_static(notice, text);
  lv_obj_remove_flag(notice, LV_OBJ_FLAG_HIDDEN);
  notice_until_ms = lv_tick_get() + duration_ms;
  notice_shown = true;
}

void OnKnobStep(int8_t step) {
  if (!power.OnInput(lv_tick_get())) {
    return;  // this step only woke the screen
  }
  if (InputTestOpen()) {
    test->OnKnobStep(step);  // raw hardware direction
    return;
  }
  if (screen_manager::ModalOpen()) {
    return;  // nothing to scroll on the setup and update screens
  }
  input::AddSteps(app->State().settings.knob_reversed ? -step : step);
}

void OnButtonSample(bool raw_pressed, uint32_t now_ms) {
  if (raw_pressed && !last_button_raw && !power.OnInput(now_ms)) {
    swallow_button = true;  // ignore this whole press: it only woke the screen
  }
  last_button_raw = raw_pressed;
  if (swallow_button) {
    if (!raw_pressed) {
      swallow_button = false;
    }
    raw_pressed = false;
  }

  const bool input_test_open = InputTestOpen();
  if (input_test_open) {
    test->OnButtonSample(raw_pressed, now_ms);
  }
  switch (button.Update(raw_pressed, now_ms).gesture) {
    case core::ButtonGesture::kClick:
      if (screen_manager::ModalOpen()) {
        screen_manager::ModalClick();
      } else if (!input_test_open) {
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
      } else if (screen_manager::ModalOpen()) {
        screen_manager::ModalLongPress();
      }
      break;
    case core::ButtonGesture::kNone:
      break;
  }
}

bool OnTouch(bool touching, int16_t x, int16_t y) {
  if (touching && !last_touching && !power.OnInput(lv_tick_get())) {
    swallow_touch = true;  // this touch only woke the screen
  }
  last_touching = touching;
  if (swallow_touch) {
    if (!touching) {
      swallow_touch = false;
    }
    test->OnTouch(false, 0, 0);
    return false;
  }
  test->OnTouch(touching, x, y);
  return touching;
}

void Update(uint32_t now_ms) {
  if (screen_manager::ModalOpen()) {
    static_cast<void>(power.OnInput(now_ms));  // no screen timeout during setup or an update
  }
  power.Update(app->State(), now_ms);
  if (notice_shown && static_cast<int32_t>(now_ms - notice_until_ms) >= 0) {
    lv_obj_add_flag(notice, LV_OBJ_FLAG_HIDDEN);
    notice_shown = false;
  }
  // A theme change in Settings restyles every screen in place: no reboot, no rebuild.
  const core::ThemeDescriptor& wanted = core::ThemeFor(app->State().settings.theme);
  if (&wanted != &theme::Current()) {
    theme::Apply(wanted);
  }
  screen_manager::Update(*app, test->State(), now_ms);
}

const char* CurrentScreenName() {
  switch (screen_manager::Current()) {
    case screen_manager::ScreenId::kNav:
      return "nav";
    case screen_manager::ScreenId::kMusic:
      return "music";
    case screen_manager::ScreenId::kSettings:
      return "settings";
    case screen_manager::ScreenId::kCall:
      return "call";
    case screen_manager::ScreenId::kInputTest:
      return "input-test";
    case screen_manager::ScreenId::kWifiSetup:
      return "wifi-setup";
    case screen_manager::ScreenId::kUpdate:
      return "update";
  }
  return "?";
}

int BacklightPercent() {
  if (InputTestOpen()) {
    return test->State().backlight_percent;
  }
  if (!power.Awake()) {
    return 0;  // screen timeout
  }
  return core::DayNightBrightness(app->State().settings, app->State().clock);
}

}  // namespace sarathi::ui::app_shell
