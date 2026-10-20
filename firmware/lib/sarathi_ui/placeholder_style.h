// The only place Phase 1 placeholder screens get colours and fonts from, until the theme
// system (ADR-0003) replaces it. Font roles and sizes follow ADR-0003's legibility table;
// colours meet its contrast minimums on black.

#pragma once

#include <cstdint>

#include <lvgl.h>

namespace sarathi::ui::style {

// ---- Fonts (LVGL built-in Montserrat, ADR-0003) ----
inline const lv_font_t* Primary() { return &lv_font_montserrat_48; }      // 17' at 75 cm
inline const lv_font_t* Essential() { return &lv_font_montserrat_46; }    // 16'
inline const lv_font_t* Supporting() { return &lv_font_montserrat_34; }   // 12'
inline const lv_font_t* StoppedOnly() { return &lv_font_montserrat_28; }  // 10', settings

// ---- Colours (RGB; contrast against black, WCAG 2) ----
constexpr uint32_t kBackground = 0x000000;
constexpr uint32_t kText = 0xFFFFFF;            // 21:1
constexpr uint32_t kTextSupporting = 0xC8C8C8;  // 12.6:1
constexpr uint32_t kAccent = 0xFFB000;          // focus ring, highlights; 11.5:1
constexpr uint32_t kWarning = 0xFFB000;
// Button fills carry white essential text, so they must be dark enough for 7:1.
constexpr uint32_t kAcceptFill = 0x1B5E20;   // white on it: 7.9:1
constexpr uint32_t kRejectFill = 0x8E1212;   // white on it: 9.4:1
constexpr uint32_t kNeutralFill = 0x303030;  // white on it: 13.2:1

// ---- Layout ----
constexpr int32_t kScreenSize = 480;
constexpr int32_t kContentWidth = 380;  // widest text line, centred, inside the circle
constexpr int32_t kFocusOutline = 4;

// Black, non-scrolling screen.
inline lv_obj_t* MakeScreen() {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  return screen;
}

// A visible ring when the knob focuses something.
inline void AddFocusRing(lv_obj_t* obj) {
  lv_obj_set_style_outline_color(obj, lv_color_hex(kAccent), LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(obj, kFocusOutline, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_pad(obj, kFocusOutline, LV_STATE_FOCUS_KEY);
}

// A button with a centred text label. Returns the button; the label is its first child.
inline lv_obj_t* MakeButton(lv_obj_t* parent, const char* text, const lv_font_t* font,
                            uint32_t fill, int32_t w, int32_t h) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_set_size(button, w, h);
  lv_obj_set_style_bg_color(button, lv_color_hex(fill), 0);
  lv_obj_set_style_radius(button, h / 2, 0);
  AddFocusRing(button);
  lv_obj_t* label = lv_label_create(button);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
  lv_label_set_text_static(label, text);
  lv_obj_center(label);
  return button;
}

}  // namespace sarathi::ui::style
