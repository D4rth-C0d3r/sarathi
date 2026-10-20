#include "input_test_screen.h"

#include "static_label.h"
#include "theme_binding.h"

namespace sarathi::ui::input_test_screen {
namespace {

using core::ButtonGesture;

constexpr int32_t kLineSpacing = 60;
constexpr int32_t kDotSize = 24;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
StaticLabel<32> knob_line;
StaticLabel<32> button_line;
StaticLabel<32> touch_line;
StaticLabel<32> backlight_line;
lv_obj_t* touch_dot = nullptr;
State shown;
bool first_update = true;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

const char* GestureName(ButtonGesture g) {
  switch (g) {
    case ButtonGesture::kClick:
      return "click";
    case ButtonGesture::kDoubleClick:
      return "double";
    case ButtonGesture::kLongPress:
      return "long";
    case ButtonGesture::kNone:
      break;
  }
  return "-";
}

}  // namespace

lv_obj_t* Create() {
  lv_obj_t* screen = theme::MakeScreen();
  knob_line.Create(screen, theme::Text::kSupporting, -kLineSpacing * 3 / 2);
  button_line.Create(screen, theme::Text::kSupporting, -kLineSpacing / 2);
  touch_line.Create(screen, theme::Text::kSupporting, kLineSpacing / 2);
  backlight_line.Create(screen, theme::Text::kSupporting, kLineSpacing * 3 / 2);

  StaticLabel<24> hint;
  hint.Create(screen, theme::Text::kStoppedOnly, kLineSpacing * 5 / 2);
  lv_label_set_text_static(hint.Obj(), "Hold button to exit");

  touch_dot = lv_obj_create(screen);
  lv_obj_remove_style_all(touch_dot);
  lv_obj_set_size(touch_dot, kDotSize, kDotSize);
  lv_obj_add_style(touch_dot, theme::AccentDotStyle(), 0);
  lv_obj_remove_flag(touch_dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
  return screen;
}

void Update(const State& s) {
  knob_line.Set("Knob: %d", s.knob_count);
  button_line.Set("Button: %s (%s)", s.button_pressed ? "down" : "up", GestureName(s.last_gesture));
  backlight_line.Set("Backlight: %d%%", s.backlight_percent);

  const bool touch_moved = s.touch_x != shown.touch_x || s.touch_y != shown.touch_y;
  if (first_update || s.touching != shown.touching || (s.touching && touch_moved)) {
    if (s.touching) {
      touch_line.Set("Touch: %d, %d", s.touch_x, s.touch_y);
      lv_obj_set_pos(touch_dot, s.touch_x - (kDotSize / 2), s.touch_y - (kDotSize / 2));
      lv_obj_remove_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
    } else {
      touch_line.Set("%s", "Touch: -");
      lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
    }
  }
  shown = s;
  first_update = false;
}

}  // namespace sarathi::ui::input_test_screen
