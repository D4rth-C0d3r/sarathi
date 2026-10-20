// Racing game: racing red and yellow on near-black, a thick tachometer-style ring, sharp
// corners, and a segmented rev bar and chequered strip at the sides.

#include "theme_registry.h"

namespace sarathi::core {

const ThemeDescriptor kRacingTheme = {
    ThemeId::kRacing,
    "Racing game",
    "Racing",
    ThemePalette{
        0x0B0B0D,  // background
        0x24242A,  // surface
        0xFFFFFF,  // text_primary
        0xE0E0E0,  // text_supporting
        0xFF2D2D,  // accent
        0xFFD60A,  // warning
        0xFF2D2D,  // danger
        0xFFD60A,  // route_line
        0x5C5030,  // route_behind
        0xFFD60A,  // maneuver
        0xFFFFFF,  // rider
        0x1B5E20,  // fill_accept
        0x9A1010,  // fill_reject
        0x2E2E36,  // fill_neutral
        0xFFFFFF,  // button_text
    },
    ThemeTypography{{48, 46, 34, 28}},
    ThemeComponentStyle{
        4,   // focus_outline_px
        4,   // button_radius_px: sharp
        4,   // surface_radius_px
        16,  // ring_width_px: tachometer-thick
        16,  // arrow_stroke_px
        10,  // route_line_px
        6,   // route_behind_px
        64,  // list_row_height_px
    },
    Ornament::kRacingTach,
};

}  // namespace sarathi::core
