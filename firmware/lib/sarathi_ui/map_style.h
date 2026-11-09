// The map's look for a theme (ADR-0009 "Per-theme map styles"): the theme's map data
// (core::ThemeMapStyle, checked by the legibility tests) as raw pixels for the renderer.

#pragma once

#include "map_render.h"
#include "theme.h"

namespace sarathi::ui {

// Raw pixels (theme::Pixel), road widths scaled for `metres_per_px`.
[[nodiscard]] core::MapStyle MapStyleFor(const core::ThemeDescriptor& theme, float metres_per_px);

}  // namespace sarathi::ui
