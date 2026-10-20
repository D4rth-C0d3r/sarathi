#include "theme_binding.h"

#include <array>
#include <cstddef>

#include "layout.h"
#include "ornament.h"
#include "theme_registry.h"

namespace sarathi::ui::theme {
namespace {

constexpr size_t kTextCount = 5;
constexpr size_t kFillCount = 3;
constexpr size_t kMaxScreens = 8;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
bool initialised = false;
const core::ThemeDescriptor* current = &core::kClassicTheme;
lv_style_t screen_style;
std::array<lv_style_t, kTextCount> text_styles;
std::array<lv_style_t, kFillCount> button_styles;
std::array<lv_style_t, core::kFontRoleCount> button_text_styles;
lv_style_t focus_style;
lv_style_t edit_style;
lv_style_t list_row_style;
lv_style_t gauge_track_style;
lv_style_t gauge_indicator_style;
lv_style_t accent_dot_style;
lv_style_t card_style;
core::OrnamentShapes ornament;  // the current theme's, rebuilt by Apply()
std::array<lv_obj_t*, kMaxScreens> ornament_layers{};
size_t ornament_layer_count = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

constexpr int32_t kCardRadius = 24;
constexpr int32_t kCardPadX = 14;
constexpr int32_t kCardPadY = 4;

size_t Index(Text t) { return static_cast<size_t>(t); }
size_t Index(Fill f) { return static_cast<size_t>(f); }

void InitStyles() {
  lv_style_init(&screen_style);
  for (auto& s : text_styles) {
    lv_style_init(&s);
  }
  for (auto& s : button_styles) {
    lv_style_init(&s);
  }
  for (auto& s : button_text_styles) {
    lv_style_init(&s);
  }
  lv_style_init(&focus_style);
  lv_style_init(&edit_style);
  lv_style_init(&list_row_style);
  lv_style_init(&gauge_track_style);
  lv_style_init(&gauge_indicator_style);
  lv_style_init(&accent_dot_style);
  lv_style_init(&card_style);
}

void SetText(Text role, core::FontRole font, core::Rgb color) {
  lv_style_t* s = &text_styles.at(Index(role));
  lv_style_set_text_font(s, Font(font));
  lv_style_set_text_color(s, Color(color));
}

void SetButton(Fill fill, core::Rgb color, int32_t radius, core::Rgb text) {
  lv_style_t* s = &button_styles.at(Index(fill));
  lv_style_set_bg_color(s, Color(color));
  lv_style_set_bg_opa(s, LV_OPA_COVER);
  lv_style_set_radius(s, radius);
  lv_style_set_text_color(s, Color(text));
  lv_style_set_shadow_width(s, 0);
}

void WriteStyles(const core::ThemeDescriptor& t) {
  const core::ThemePalette& p = t.palette;
  const core::ThemeComponentStyle& c = t.components;

  lv_style_set_bg_color(&screen_style, Color(p.background));
  lv_style_set_bg_opa(&screen_style, LV_OPA_COVER);

  SetText(Text::kPrimary, core::FontRole::kPrimary, p.text_primary);
  SetText(Text::kEssential, core::FontRole::kEssential, p.text_primary);
  SetText(Text::kSupporting, core::FontRole::kSupporting, p.text_supporting);
  SetText(Text::kStoppedOnly, core::FontRole::kStoppedOnly, p.text_primary);
  SetText(Text::kWarning, core::FontRole::kSupporting, p.warning);

  const int32_t radius = c.button_radius_px == 0 ? LV_RADIUS_CIRCLE : c.button_radius_px;
  SetButton(Fill::kAccept, p.fill_accept, radius, p.button_text);
  SetButton(Fill::kReject, p.fill_reject, radius, p.button_text);
  SetButton(Fill::kNeutral, p.fill_neutral, radius, p.button_text);
  for (size_t role = 0; role < core::kFontRoleCount; ++role) {
    lv_style_set_text_font(&button_text_styles.at(role), Font(static_cast<core::FontRole>(role)));
  }

  lv_style_set_outline_color(&focus_style, Color(p.accent));
  lv_style_set_outline_width(&focus_style, c.focus_outline_px);
  lv_style_set_outline_pad(&focus_style, c.focus_outline_px);
  lv_style_set_outline_opa(&focus_style, LV_OPA_COVER);

  lv_style_set_border_color(&edit_style, Color(p.accent));
  lv_style_set_border_width(&edit_style, c.focus_outline_px);
  lv_style_set_border_opa(&edit_style, LV_OPA_COVER);

  lv_style_set_bg_color(&list_row_style, Color(p.surface));
  lv_style_set_bg_opa(&list_row_style, LV_OPA_COVER);
  lv_style_set_radius(&list_row_style, c.surface_radius_px);
  lv_style_set_height(&list_row_style, c.list_row_height_px);
  lv_style_set_text_font(&list_row_style, Font(core::FontRole::kStoppedOnly));
  lv_style_set_text_color(&list_row_style, Color(p.text_primary));
  lv_style_set_shadow_width(&list_row_style, 0);

  lv_style_set_arc_color(&gauge_track_style, Color(p.surface));
  lv_style_set_arc_width(&gauge_track_style, c.ring_width_px);
  lv_style_set_arc_rounded(&gauge_track_style, true);
  lv_style_set_bg_opa(&gauge_track_style, LV_OPA_TRANSP);
  lv_style_set_border_width(&gauge_track_style, 0);
  lv_style_set_pad_all(&gauge_track_style, 0);
  lv_style_set_arc_color(&gauge_indicator_style, Color(p.accent));
  lv_style_set_arc_width(&gauge_indicator_style, c.ring_width_px);
  lv_style_set_arc_rounded(&gauge_indicator_style, true);

  lv_style_set_bg_color(&card_style, Color(p.background));
  lv_style_set_bg_opa(&card_style,
                      static_cast<lv_opa_t>(core::kCardOpacityPercent * LV_OPA_COVER / 100));
  lv_style_set_radius(&card_style, kCardRadius);
  lv_style_set_pad_hor(&card_style, kCardPadX);
  lv_style_set_pad_ver(&card_style, kCardPadY);
  lv_style_set_border_width(&card_style, 0);

  lv_style_set_bg_color(&accent_dot_style, Color(p.accent));
  lv_style_set_bg_opa(&accent_dot_style, LV_OPA_COVER);
  lv_style_set_radius(&accent_dot_style, LV_RADIUS_CIRCLE);
  lv_style_set_border_width(&accent_dot_style, 0);
}

lv_value_precise_t ScreenCoord(int16_t centred) {
  return static_cast<lv_value_precise_t>(centred + layout::kCenter);
}

void DrawOrnament(lv_event_t* event) {
  lv_layer_t* layer = lv_event_get_layer(event);
  const core::ThemePalette& p = current->palette;

  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  for (size_t i = 0; i < ornament.line_count; ++i) {
    const core::OrnamentLine& l = ornament.lines.at(i);
    line.color = Color(core::OrnamentRgb(p, l.color));
    line.width = l.width;
    line.p1 = {ScreenCoord(l.x1), ScreenCoord(l.y1)};
    line.p2 = {ScreenCoord(l.x2), ScreenCoord(l.y2)};
    lv_draw_line(layer, &line);
  }

  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  rect.bg_opa = LV_OPA_COVER;
  for (size_t i = 0; i < ornament.rect_count; ++i) {
    const core::OrnamentRect& r = ornament.rects.at(i);
    rect.bg_color = Color(core::OrnamentRgb(p, r.color));
    const int32_t x = r.x + layout::kCenter;
    const int32_t y = r.y + layout::kCenter;
    const lv_area_t area{x, y, x + r.w - 1, y + r.h - 1};
    lv_draw_rect(layer, &rect, &area);
  }
}

// A full-screen, transparent layer behind everything else on the screen. It only draws the
// ornament (static: it redraws only when something above it does, or the theme changes).
void AddOrnamentLayer(lv_obj_t* screen) {
  lv_obj_t* layer = lv_obj_create(screen);
  lv_obj_remove_style_all(layer);
  lv_obj_set_size(layer, layout::kScreenSize, layout::kScreenSize);
  lv_obj_remove_flag(layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(layer, DrawOrnament, LV_EVENT_DRAW_MAIN, nullptr);
  if (ornament_layer_count < ornament_layers.size()) {
    ornament_layers.at(ornament_layer_count++) = layer;
  }
}

}  // namespace

void Apply(const core::ThemeDescriptor& theme) {
  if (!initialised) {
    InitStyles();
    initialised = true;
  }
  current = &theme;
  WriteStyles(theme);
  ornament = core::BuildOrnament(theme.ornament);
  for (size_t i = 0; i < ornament_layer_count; ++i) {
    lv_obj_invalidate(ornament_layers.at(i));
  }

  // LVGL's default theme (dark) supplies widget basics such as scrollbars; re-seed its
  // colours from ours so nothing keeps the previous theme's accent.
  lv_display_t* display = lv_display_get_default();
  if (display != nullptr) {
    lv_theme_t* base =
        lv_theme_default_init(display, Color(theme.palette.accent), Color(theme.palette.danger),
                              true, Font(core::FontRole::kStoppedOnly));
    lv_display_set_theme(display, base);
  }
  lv_obj_report_style_change(nullptr);  // restyle and redraw every object
}

const core::ThemeDescriptor& Current() { return *current; }

lv_color_t Color(core::Rgb rgb) { return lv_color_hex(rgb); }

const lv_font_t* Font(core::FontRole role) {
  // Only the sizes in core::kBuiltInFontPx are compiled in; the legibility test keeps every
  // registered theme on them.
  switch (current->fonts.Px(role)) {
    case 48:
      return &lv_font_montserrat_48;
    case 46:
      return &lv_font_montserrat_46;
    case 34:
      return &lv_font_montserrat_34;
    case 28:
    default:
      return &lv_font_montserrat_28;
  }
}

lv_style_t* ScreenStyle() { return &screen_style; }
lv_style_t* TextStyle(Text role) { return &text_styles.at(Index(role)); }
lv_style_t* ButtonStyle(Fill fill) { return &button_styles.at(Index(fill)); }
lv_style_t* ButtonTextStyle(core::FontRole role) {
  return &button_text_styles.at(static_cast<size_t>(role));
}
lv_style_t* FocusStyle() { return &focus_style; }
lv_style_t* EditStyle() { return &edit_style; }
lv_style_t* ListRowStyle() { return &list_row_style; }
lv_style_t* GaugeTrackStyle() { return &gauge_track_style; }
lv_style_t* GaugeIndicatorStyle() { return &gauge_indicator_style; }
lv_style_t* AccentDotStyle() { return &accent_dot_style; }
lv_style_t* CardStyle() { return &card_style; }

lv_obj_t* MakeScreen() {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_add_style(screen, ScreenStyle(), 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  AddOrnamentLayer(screen);  // first child, so it draws first
  return screen;
}

lv_obj_t* MakeButton(lv_obj_t* parent, const char* text, Fill fill, core::FontRole font, int32_t w,
                     int32_t h) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_set_size(button, w, h);
  lv_obj_add_style(button, ButtonStyle(fill), 0);
  lv_obj_add_style(button, FocusStyle(), LV_STATE_FOCUSED);
  lv_obj_t* label = lv_label_create(button);
  lv_obj_add_style(label, ButtonTextStyle(font), 0);
  lv_label_set_text_static(label, text);
  lv_obj_center(label);
  return button;
}

}  // namespace sarathi::ui::theme
