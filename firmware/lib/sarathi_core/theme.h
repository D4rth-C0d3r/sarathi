// A visual theme as plain data (ADR-0003). sarathi_ui binds it to LVGL styles; keeping it
// here lets the legibility rules run as native tests over every registered theme.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app_state.h"

namespace sarathi::core {

using Rgb = uint32_t;  // 0xRRGGBB

// Colours by role, never by look. Screens and components only ever ask for a role.
struct ThemePalette {
  Rgb background;       // screen
  Rgb surface;          // cards, gauge tracks, list rows
  Rgb text_primary;     // primary and essential text
  Rgb text_supporting;  // supporting text
  Rgb accent;           // focus ring, gauge indicator, highlights
  Rgb warning;          // warnings shown as text ("Stop to change settings")
  Rgb danger;           // non-text danger cues
  Rgb route_line;       // route ahead
  Rgb route_behind;     // route already ridden (dim)
  Rgb maneuver;         // maneuver arrow and marker
  Rgb rider;            // rider chevron on the map
  Rgb fill_accept;      // button fills; button_text sits on them
  Rgb fill_reject;
  Rgb fill_neutral;
  Rgb button_text;
};

// Font roles from ADR-0003's legibility table.
enum class FontRole : uint8_t { kPrimary, kEssential, kSupporting, kStoppedOnly };
constexpr size_t kFontRoleCount = 4;

// The LVGL built-in Montserrat sizes compiled in (lv_conf.h). Themes may only use these until
// the font work (ADR-0003).
constexpr std::array<uint8_t, 4> kBuiltInFontPx = {28, 34, 46, 48};

struct ThemeTypography {
  std::array<uint8_t, kFontRoleCount> px;  // indexed by FontRole

  [[nodiscard]] uint8_t Px(FontRole role) const { return px[static_cast<size_t>(role)]; }
};

// Component parameters, in pixels.
struct ThemeComponentStyle {
  uint8_t focus_outline_px;  // ring around the knob-focused control
  uint8_t button_radius_px;  // 0 = fully rounded ends
  uint8_t surface_radius_px;
  uint8_t ring_width_px;    // ring gauge thickness
  uint8_t arrow_stroke_px;  // maneuver arrow stroke at 100 px size
  uint8_t route_line_px;    // route ahead
  uint8_t route_behind_px;  // route behind
  uint8_t list_row_height_px;
};

// Text over the map sits on cards of the background colour at this opacity, so the route
// stays faintly visible underneath. The legibility rules check text against the worst-case
// blend of card and route line.
constexpr uint8_t kCardOpacityPercent = 80;

// Decorative layer drawn behind everything, inside the ornament zone (ornament.h).
enum class Ornament : uint8_t { kNone, kHudReticle, kRacingTach };

struct ThemeDescriptor {
  ThemeId id;
  const char* name;        // in the theme list
  const char* short_name;  // where space is tight ("Theme: HUD")
  ThemePalette palette;
  ThemeTypography fonts;
  ThemeComponentStyle components;
  Ornament ornament;
};

}  // namespace sarathi::core
