// Fighter jet HUD: phosphor green on black, thin lines, square corners. Palette and component
// parameters only for now; its reticle and ladder ornaments come later (ADR-0003).

#include "theme_registry.h"

namespace sarathi::core {

const ThemeDescriptor kFighterHudTheme = {
    ThemeId::kFighterHud,
    "Fighter jet HUD",
    "HUD",
    ThemePalette{
        0x000000,  // background
        0x0A1F0A,  // surface
        0x7CFF7C,  // text_primary
        0x4FD34F,  // text_supporting
        0x7CFF7C,  // accent
        0xFFD24A,  // warning
        0xFF6B6B,  // danger
        0x7CFF7C,  // route_line
        0x2E6B2E,  // route_behind
        0x7CFF7C,  // maneuver
        0xE6FFE6,  // rider
        0x0F3D0F,  // fill_accept
        0x4A0E0E,  // fill_reject
        0x102810,  // fill_neutral
        0xCCFFCC,  // button_text
    },
    ThemeTypography{{48, 46, 34, 28}},
    ThemeComponentStyle{
        3,   // focus_outline_px
        6,   // button_radius_px: squared-off
        4,   // surface_radius_px
        4,   // ring_width_px
        7,   // arrow_stroke_px
        4,   // route_line_px
        3,   // route_behind_px
        60,  // list_row_height_px
    },
    Ornament::kNone,  // kHudReticle once ornaments are implemented
};

}  // namespace sarathi::core
