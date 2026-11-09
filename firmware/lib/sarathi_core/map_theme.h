// How the route looks at each traffic level in a theme (ADR-0009): never by colour alone. Pure
// data, so the legibility rules check it (test_theme_legibility).

#pragma once

#include <cstdint>

#include "app_state.h"
#include "map_render.h"
#include "theme.h"

namespace sarathi::core {

struct RouteLook {
  Rgb color = 0;
  uint8_t width_px = 0;
  RoutePattern pattern = RoutePattern::kSolid;
};

// Free solid in the route colour; slow long dashes; jam 4 px wider with cross-ticks; unknown
// 2 px thinner and dimmed. A stale route draws dimmed at every level.
[[nodiscard]] RouteLook RouteLookFor(const ThemeDescriptor& theme, TrafficLevel level, bool stale);

}  // namespace sarathi::core
