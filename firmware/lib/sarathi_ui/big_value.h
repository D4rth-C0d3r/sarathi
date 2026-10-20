// Semantic component: one big primary value (distance, speed) with an optional supporting
// caption under it.

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "layout.h"
#include "static_label.h"

namespace sarathi::ui {

class BigValue {
 public:
  void Create(lv_obj_t* parent, int32_t y_offset, int32_t width = layout::kContentWidth) {
    value_.Create(parent, theme::Text::kPrimary, y_offset, width);
    caption_.Create(parent, theme::Text::kSupporting, y_offset + kCaptionGap, width);
  }

  void SetValue(const char* text) { value_.Set("%s", text); }
  void SetCaption(const char* text) { caption_.Set("%s", text); }

 private:
  static constexpr int32_t kCaptionGap = 52;  // below a 48 px value
  StaticLabel<24> value_;
  StaticLabel<48> caption_;
};

}  // namespace sarathi::ui
