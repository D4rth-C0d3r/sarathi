#include "map_theme.h"

namespace sarathi::core {

static_assert(kMapRoadClasses == static_cast<size_t>(kRoadClasses),
              "theme and renderer road classes");

RouteLook RouteLookFor(const ThemeDescriptor& theme, TrafficLevel level, bool stale) {
  constexpr uint8_t kJamExtraPx = 4;
  constexpr uint8_t kUnknownLessPx = 2;
  const ThemePalette& p = theme.palette;
  const uint8_t w = theme.components.route_line_px;
  const auto dim = [&](Rgb c) { return stale ? p.route_behind : c; };
  switch (level) {
    case TrafficLevel::kSlow:
      return RouteLook{dim(p.traffic_slow), w, RoutePattern::kDashed};
    case TrafficLevel::kJam:
      return RouteLook{dim(p.traffic_jam), static_cast<uint8_t>(w + kJamExtraPx),
                       RoutePattern::kTicks};
    case TrafficLevel::kUnknown:
      return RouteLook{p.route_behind,
                       static_cast<uint8_t>(w > kUnknownLessPx + 1 ? w - kUnknownLessPx : w),
                       RoutePattern::kSolid};
    case TrafficLevel::kFree:
    default:
      return RouteLook{dim(p.route_line), w, RoutePattern::kSolid};
  }
}

}  // namespace sarathi::core
