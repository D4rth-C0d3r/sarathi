#include "input_test_screen.h"

#include <array>
#include <cstdio>

#include <lvgl.h>

namespace sarathi::ui::input_test_screen {
namespace {

using core::ButtonGesture;

constexpr int32_t kLineSpacing = 60;
constexpr int32_t kDotSize = 24;
constexpr uint32_t kTextColor = 0xFFFFFF;
constexpr uint32_t kDotColor = 0xFFA000;

// Label text lives in fixed buffers (lv_label_set_text_static), so updates never allocate.
using Text = std::array<char, 32>;

struct Line {
  lv_obj_t* label = nullptr;
  Text text{};
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
Line knob_line;
Line button_line;
Line touch_line;
Line backlight_line;
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
    case ButtonGesture::kNone:
      break;
  }
  return "-";
}

void MakeLine(lv_obj_t* screen, Line& line, int32_t y_offset) {
  line.label = lv_label_create(screen);
  lv_obj_set_style_text_font(line.label, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(line.label, lv_color_hex(kTextColor), 0);
  line.text[0] = '\0';
  lv_label_set_text_static(line.label, line.text.data());
  lv_obj_align(line.label, LV_ALIGN_CENTER, 0, y_offset);
}

// Re-points the label at its (rewritten) buffer so LVGL re-measures and redraws it.
template <typename... Args>
void SetLine(Line& line, const char* format, Args... args) {
  std::snprintf(line.text.data(), line.text.size(), format, args...);  // NOLINT(*-vararg)
  lv_label_set_text_static(line.label, line.text.data());
}

}  // namespace

void Create() {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  MakeLine(screen, knob_line, -kLineSpacing * 3 / 2);
  MakeLine(screen, button_line, -kLineSpacing / 2);
  MakeLine(screen, touch_line, kLineSpacing / 2);
  MakeLine(screen, backlight_line, kLineSpacing * 3 / 2);

  touch_dot = lv_obj_create(screen);
  lv_obj_set_size(touch_dot, kDotSize, kDotSize);
  lv_obj_set_style_radius(touch_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(touch_dot, lv_color_hex(kDotColor), 0);
  lv_obj_set_style_border_width(touch_dot, 0, 0);
  lv_obj_remove_flag(touch_dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);

  lv_screen_load(screen);
}

void Update(const State& s) {
  if (first_update || s.knob_count != shown.knob_count) {
    SetLine(knob_line, "Knob: %d", s.knob_count);
  }
  if (first_update || s.button_pressed != shown.button_pressed ||
      s.last_gesture != shown.last_gesture) {
    SetLine(button_line, "Button: %s (%s)", s.button_pressed ? "down" : "up",
            GestureName(s.last_gesture));
  }
  const bool touch_moved = s.touch_x != shown.touch_x || s.touch_y != shown.touch_y;
  if (first_update || s.touching != shown.touching || (s.touching && touch_moved)) {
    if (s.touching) {
      SetLine(touch_line, "Touch: %d, %d", s.touch_x, s.touch_y);
      lv_obj_set_pos(touch_dot, s.touch_x - (kDotSize / 2), s.touch_y - (kDotSize / 2));
      lv_obj_remove_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
    } else {
      SetLine(touch_line, "%s", "Touch: -");
      lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (first_update || s.backlight_percent != shown.backlight_percent) {
    SetLine(backlight_line, "Backlight: %d%%", s.backlight_percent);
  }
  shown = s;
  first_update = false;
}

}  // namespace sarathi::ui::input_test_screen
