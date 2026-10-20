// Binds a core::ThemeDescriptor to LVGL (ADR-0003). Components never set colours or fonts
// directly: they add these shared styles, or read Current() when they draw.
//
// Apply() rewrites the styles in place and tells LVGL to restyle every object, so switching
// theme at runtime needs no reboot and no rebuilt screens. Call from the UI task only.

#pragma once

#include <cstdint>

#include <lvgl.h>

#include "theme.h"

namespace sarathi::ui::theme {

enum class Text : uint8_t { kPrimary, kEssential, kSupporting, kStoppedOnly, kWarning };
enum class Fill : uint8_t { kAccept, kReject, kNeutral };

// The first call also initialises the styles. Safe to call again with the same theme.
void Apply(const core::ThemeDescriptor& theme);

[[nodiscard]] const core::ThemeDescriptor& Current();

[[nodiscard]] lv_color_t Color(core::Rgb rgb);
[[nodiscard]] const lv_font_t* Font(core::FontRole role);

// Shared styles. Add with lv_obj_add_style(obj, style, selector).
lv_style_t* ScreenStyle();                         // background
lv_style_t* TextStyle(Text role);                  // font + colour
lv_style_t* ButtonStyle(Fill fill);                // fill, radius, label colour
lv_style_t* ButtonTextStyle(core::FontRole role);  // label font
// Use with LV_STATE_FOCUSED (not FOCUS_KEY): screens focus their first control from code when
// shown, and the ring must show then too.
lv_style_t* FocusStyle();
lv_style_t* EditStyle();            // use with LV_STATE_EDITED
lv_style_t* ListRowStyle();         // list menu rows
lv_style_t* GaugeTrackStyle();      // ring gauge, LV_PART_MAIN
lv_style_t* GaugeIndicatorStyle();  // ring gauge, LV_PART_INDICATOR
lv_style_t* AccentDotStyle();       // small accent markers
// Background-coloured card at core::kCardOpacityPercent, for text drawn over the map. On a
// label it becomes a padded bubble sized to the text.
lv_style_t* CardStyle();

// A theme-styled, non-scrolling screen.
lv_obj_t* MakeScreen();

// A themed button with a centred label (the button's first child).
lv_obj_t* MakeButton(lv_obj_t* parent, const char* text, Fill fill, core::FontRole font, int32_t w,
                     int32_t h);

}  // namespace sarathi::ui::theme
