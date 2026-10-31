// Follows the rider along a route received from the phone (protocol v1 `route` and `pose`,
// ADR-0002, ADR-0009) and cuts the route window the Nav map draws. Pure logic: the route can
// have thousands of points, so it lives in caller storage (PSRAM on the device).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app_state.h"
#include "route_geometry.h"

namespace sarathi::core {

constexpr size_t kMaxTrackedManeuvers = 256;
// Within this distance of the route the rider is on it, and is followed along it from the last
// match; further away the whole route is searched.
constexpr float kOnRouteM = 50.0F;
// The local search around the last match: a little back, more ahead.
constexpr float kSearchBehindM = 100.0F;
constexpr float kSearchAheadM = 600.0F;

struct TrackerStorage {
  MetricPoint* points = nullptr;
  float* cumulative = nullptr;  // distance along the route at each point
  size_t capacity = 0;
};

// Where the rider is relative to the route.
struct RouteFix {
  float along_m = 0.0F;   // distance along the route of the nearest point
  float offset_m = 0.0F;  // from the rider to that point
};

class RouteTracker {
 public:
  explicit RouteTracker(const TrackerStorage& storage) : storage_(storage) {}

  // Replaces the route with `n` points, each scaled from P's x and y (any type with x and y)
  // by `metres_per_unit`. False (and no route) if n < 2 or n exceeds the storage.
  template <typename P>
  bool SetRoute(const P* points, size_t n, float metres_per_unit) {
    Clear();
    if (n < 2 || n > storage_.capacity) {
      return false;
    }
    for (size_t i = 0; i < n; ++i) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      storage_.points[i] = {static_cast<float>(points[i].x) * metres_per_unit,
                            static_cast<float>(points[i].y) * metres_per_unit};
    }
    count_ = n;
    Measure();
    return true;
  }

  // Maneuvers in increasing vertex order, after SetRoute; false if out of order or full.
  bool AddManeuver(size_t vertex, const Maneuver& maneuver);

  void Clear();
  [[nodiscard]] bool HasRoute() const { return count_ >= 2; }
  [[nodiscard]] float LengthM() const;

  // The nearest point of the route to `rider`, searched near the last fix first.
  RouteFix Locate(MetricPoint rider);

  // The route from `behind_m` before to `ahead_m` after `fix`, relative to `rider`, simplified
  // to fit (thinned first where it's denser than the simplifier takes), heading-up on
  // `heading_deg`, with the next maneuver's point if it's in range. Doesn't touch `sequence`.
  void Window(MetricPoint rider, const RouteFix& fix, uint16_t heading_deg, float behind_m,
              float ahead_m, float tolerance_m, RouteWindow& out) const;

  // Direction of the route at `along_m`, degrees clockwise from north.
  [[nodiscard]] uint16_t HeadingAt(float along_m) const;

 private:
  void Measure();
  [[nodiscard]] size_t SegmentAt(float along_m) const;
  [[nodiscard]] MetricPoint PointAt(float along_m) const;
  [[nodiscard]] RouteFix Nearest(MetricPoint rider, size_t first_segment,
                                 size_t last_segment) const;

  TrackerStorage storage_;
  size_t count_ = 0;
  std::array<RouteManeuver, kMaxTrackedManeuvers> maneuvers_{};
  size_t maneuver_count_ = 0;
  bool has_fix_ = false;
  RouteFix last_fix_{};
};

}  // namespace sarathi::core
