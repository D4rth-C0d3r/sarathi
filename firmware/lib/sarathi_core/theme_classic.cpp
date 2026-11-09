// Classic: the readable default. White and grey text on black, amber accents, bold strokes.

#include "theme_registry.h"

namespace sarathi::core {

const ThemeDescriptor kClassicTheme = {
    ThemeId::kClassic,
    "Classic",
    "Classic",
    ThemePalette{
        0x000000,  // background
        0x1E1E1E,  // surface
        0xFFFFFF,  // text_primary
        0xC8C8C8,  // text_supporting
        0xFFB000,  // accent
        0xFFB000,  // warning
        0xFF5A4F,  // danger
        0x4FC3F7,  // route_line
        0x5A5A5A,  // route_behind
        0xFFB000,  // maneuver
        0xFFFFFF,  // rider
        0xFFB300,  // traffic_slow
        0xFF3B30,  // traffic_jam
        0x1B5E20,  // fill_accept
        0x8E1212,  // fill_reject
        0x303030,  // fill_neutral
        0xFFFFFF,  // button_text
    },
    ThemeTypography{{48, 46, 34, 28}},
    ThemeComponentStyle{
        4,   // focus_outline_px
        0,   // button_radius_px: fully rounded
        16,  // surface_radius_px
        10,  // ring_width_px
        14,  // arrow_stroke_px
        10,  // route_line_px
        6,   // route_behind_px
        64,  // list_row_height_px
    },
    Ornament::kNone,
    // Map: filled roads on dark ground, quiet greys under the light-blue route.
    ThemeMapStyle{
        0x000000,  // ground
        true,      // fill_areas
        0x1E3A5F,  // water
        0x1E3F2A,  // park
        {0x5C6068, 0x5C6068, 0x535760, 0x4A4E56, 0x42464D, 0x383B41, 0x2E3136},
        {7, 7, 6, 5, 4, 3, 2},
        0,  // glow_extra_px
        {},
        2,  // route_casing_px
    },
};

}  // namespace sarathi::core
