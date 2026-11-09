// The map's look for a theme (ADR-0009 "Per-theme map styles"). Provisional for step G4: a
// quiet, neutral road style under the theme's route colour, so the route and the maneuver stand
// out. Step G6 replaces it with real per-theme styles from the theme descriptors.

#pragma once

#include "map_render.h"
#include "theme.h"

namespace sarathi::ui {

// Raw pixels (theme::Pixel), road widths scaled for `metres_per_px`.
[[nodiscard]] core::MapStyle MapStyleFor(const core::ThemeDescriptor& theme, float metres_per_px);

}  // namespace sarathi::ui
