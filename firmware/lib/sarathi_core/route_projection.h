// Projects a route window (ADR-0002) onto the round screen: heading-up rotation, scale, and
// clipping to the visible circle. Pure logic; sarathi_ui only draws the segments.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app_state.h"

namespace sarathi::core {

struct ScreenPoint {
  int16_t x = 0;
  int16_t y = 0;
};

struct ScreenSegment {
  ScreenPoint a;
  ScreenPoint b;
  bool behind = false;  // part of the route the rider has already passed
};

// Each window segment can split once at the rider and is clipped to at most one piece.
constexpr size_t kMaxProjectedSegments = kMaxRoutePoints * 2;

struct ProjectionParams {
  float metres_per_px = 3.5F;
  ScreenPoint rider{240, 370};  // where the rider marker sits on screen
  ScreenPoint circle_center{240, 240};
  float circle_radius_px = 232.0F;  // visible area, inside the glass edge
};

struct ProjectedRoute {
  std::array<ScreenSegment, kMaxProjectedSegments> segments{};
  size_t count = 0;
  bool has_maneuver = false;  // maneuver point inside the circle
  ScreenPoint maneuver{};
};

// Heading-up: the direction of travel (window.heading_deg) points to the top of the screen.
void ProjectRoute(const RouteWindow& window, const ProjectionParams& params, ProjectedRoute& out);

}  // namespace sarathi::core
