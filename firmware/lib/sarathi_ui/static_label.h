// A themed label whose text lives in a fixed buffer (lv_label_set_text_static), so updating it
// never allocates, and unchanged text doesn't invalidate the screen.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <lvgl.h>

#include "theme_binding.h"

namespace sarathi::ui {

template <size_t N>
class StaticLabel {
 public:
  // `width` > 0 gives a fixed-width, centred line that ends in "..." when too long. Offsets
  // are from the parent's centre.
  void Create(lv_obj_t* parent, theme::Text role, int32_t y_offset, int32_t width = 0,
              int32_t x_offset = 0) {
    label_ = lv_label_create(parent);
    lv_obj_add_style(label_, theme::TextStyle(role), 0);
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    if (width > 0) {
      lv_obj_set_width(label_, width);
      lv_label_set_long_mode(label_, LV_LABEL_LONG_DOT);
    }
    text_[0] = '\0';
    lv_label_set_text_static(label_, text_.data());
    lv_obj_align(label_, LV_ALIGN_CENTER, x_offset, y_offset);
  }

  // printf-style. Always pass a literal format and at least one argument ("%s", text).
  template <typename... Args>
  void Set(const char* format, Args... args) {
    std::array<char, N> next{};
    std::snprintf(next.data(), next.size(), format, args...);  // NOLINT(*-vararg)
    if (std::strcmp(next.data(), text_.data()) != 0) {
      text_ = next;
      lv_label_set_text_static(label_, text_.data());  // re-measure and redraw
    }
  }

  [[nodiscard]] lv_obj_t* Obj() const { return label_; }

 private:
  lv_obj_t* label_ = nullptr;
  std::array<char, N> text_{};
};

}  // namespace sarathi::ui
