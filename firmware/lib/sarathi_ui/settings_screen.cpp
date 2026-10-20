#include "settings_screen.h"

#include "placeholder_style.h"
#include "static_label.h"

namespace sarathi::ui::settings_screen {
namespace {

// Same order as core::ThemeId.
constexpr const char* kThemeOptions = "Classic\nFighter jet HUD\nRacing game";
constexpr int32_t kControlWidth = 300;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
core::AppModel* app = nullptr;
lv_group_t* focus_group = nullptr;
OpenInputTestFn open_input_test_fn = nullptr;
lv_obj_t* controls = nullptr;  // container for everything that is locked while moving
lv_obj_t* theme_roller = nullptr;
lv_obj_t* brightness_slider = nullptr;
lv_obj_t* input_test_button = nullptr;
StaticLabel<24> brightness_value;
StaticLabel<40> lock_message;
bool shown_locked = false;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void OnTheme(lv_event_t* /*event*/) {
  app->SetTheme(static_cast<core::ThemeId>(lv_roller_get_selected(theme_roller)));
}

void OnBrightness(lv_event_t* /*event*/) {
  app->SetBrightness(static_cast<int>(lv_slider_get_value(brightness_slider)));
}

void OnInputTest(lv_event_t* /*event*/) { open_input_test_fn(); }

void SetLocked(bool locked) {
  lv_group_remove_all_objs(focus_group);
  if (locked) {
    lv_obj_add_flag(controls, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lock_message.Obj(), LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(controls, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lock_message.Obj(), LV_OBJ_FLAG_HIDDEN);
    lv_group_add_obj(focus_group, theme_roller);
    lv_group_add_obj(focus_group, brightness_slider);
    lv_group_add_obj(focus_group, input_test_button);
    lv_group_focus_obj(theme_roller);
  }
  shown_locked = locked;
}

}  // namespace

lv_obj_t* Create(lv_group_t* group, core::AppModel& model, OpenInputTestFn open_input_test) {
  app = &model;
  focus_group = group;
  open_input_test_fn = open_input_test;
  lv_obj_t* screen = style::MakeScreen();

  // Transparent full-screen container, so the whole control set hides at once.
  controls = lv_obj_create(screen);
  lv_obj_remove_style_all(controls);
  lv_obj_set_size(controls, style::kScreenSize, style::kScreenSize);
  lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

  StaticLabel<16> title;
  title.Create(controls, style::StoppedOnly(), style::kTextSupporting, -170);
  lv_label_set_text_static(title.Obj(), "Settings");

  theme_roller = lv_roller_create(controls);
  lv_roller_set_options(theme_roller, kThemeOptions, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(theme_roller, 3);
  lv_obj_set_width(theme_roller, kControlWidth);
  lv_obj_set_style_text_font(theme_roller, style::StoppedOnly(), 0);
  lv_obj_align(theme_roller, LV_ALIGN_CENTER, 0, -75);
  style::AddFocusRing(theme_roller);
  lv_obj_add_event_cb(theme_roller, OnTheme, LV_EVENT_VALUE_CHANGED, nullptr);

  brightness_value.Create(controls, style::StoppedOnly(), style::kText, 25);
  brightness_slider = lv_slider_create(controls);
  lv_slider_set_range(brightness_slider, 5, 100);
  lv_obj_set_width(brightness_slider, kControlWidth - 40);
  lv_obj_align(brightness_slider, LV_ALIGN_CENTER, 0, 70);
  style::AddFocusRing(brightness_slider);
  lv_obj_add_event_cb(brightness_slider, OnBrightness, LV_EVENT_VALUE_CHANGED, nullptr);

  input_test_button =
      style::MakeButton(controls, "Input test", style::StoppedOnly(), style::kNeutralFill, 220, 60);
  lv_obj_align(input_test_button, LV_ALIGN_CENTER, 0, 145);
  lv_obj_add_event_cb(input_test_button, OnInputTest, LV_EVENT_CLICKED, nullptr);

  lock_message.Create(screen, style::Supporting(), style::kWarning, 0, style::kContentWidth);
  lv_label_set_long_mode(lock_message.Obj(), LV_LABEL_LONG_WRAP);
  lock_message.Set("%s", "Stop to change settings");

  SetLocked(false);
  return screen;
}

void Update(const core::AppState& state, bool locked) {
  if (locked != shown_locked) {
    SetLocked(locked);
  }
  const auto theme = static_cast<uint32_t>(state.settings.theme);
  if (lv_roller_get_selected(theme_roller) != theme) {
    lv_roller_set_selected(theme_roller, theme, LV_ANIM_OFF);
  }
  if (lv_slider_get_value(brightness_slider) != state.settings.brightness_percent) {
    lv_slider_set_value(brightness_slider, state.settings.brightness_percent, LV_ANIM_OFF);
  }
  brightness_value.Set("Brightness %d%%", state.settings.brightness_percent);
}

}  // namespace sarathi::ui::settings_screen
