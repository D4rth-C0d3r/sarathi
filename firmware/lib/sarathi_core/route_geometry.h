// Route geometry for the vector route window (ADR-0002). This is the phone's job in Phase 4;
// it lives here now so the demo ride can produce realistic windows, and so it is specified by
// tests before the Android port.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app_state.h"

namespace sarathi::core {

// Metres east (x) and north (y) of the route's origin.
struct MetricPoint {
  float x = 0.0F;
  float y = 0.0F;
};

// A maneuver at a route vertex.
struct RouteManeuver {
  uint16_t vertex = 0;
  Maneuver maneuver{};
};

constexpr size_t kMaxPathPoints = 128;

// Route window settings (ADR-0002): how much route around the rider, and how closely the
// simplified line follows it.
constexpr float kWindowBehindM = 100.0F;
constexpr float kWindowAheadM = 1500.0F;
constexpr float kWindowToleranceM = 3.0F;

class RoutePath {
 public:
  // `points` and `maneuvers` must outlive the path (typically constexpr tables). Up to
  // kMaxPathPoints points; maneuvers sorted by vertex.
  RoutePath(const MetricPoint* points, size_t point_count, const RouteManeuver* maneuvers,
            size_t maneuver_count);

  [[nodiscard]] float LengthM() const { return cumulative_[count_ - 1]; }

  // Position at distance `s` along the route (clamped to the ends).
  [[nodiscard]] MetricPoint PointAt(float s) const;

  // Direction of travel at `s`, degrees clockwise from north, 0-359.
  [[nodiscard]] uint16_t HeadingAt(float s) const;

  // Distance along the route of a vertex.
  [[nodiscard]] float VertexDistanceM(size_t vertex) const { return cumulative_[vertex]; }

  // First maneuver strictly ahead of `s`, or nullptr.
  [[nodiscard]] const RouteManeuver* NextManeuver(float s) const;

  // Fills `out` with the route from `s - behind_m` to `s + ahead_m`, relative to the rider at
  // `s`, simplified (Douglas-Peucker, starting at `tolerance_m` and loosening until it fits
  // kMaxRoutePoints). Sets heading and the next maneuver's point if it lies in the window.
  // Doesn't touch `out.sequence`.
  void Window(float s, float behind_m, float ahead_m, float tolerance_m, RouteWindow& out) const;

 private:
  [[nodiscard]] size_t SegmentAt(float s) const;

  const MetricPoint* points_;
  size_t count_;
  const RouteManeuver* maneuvers_;
  size_t maneuver_count_;
  std::array<float, kMaxPathPoints> cumulative_{};
};

// The most points SimplifyPolyline takes (a full path plus interpolated window ends).
constexpr size_t kMaxSimplifyPoints = kMaxPathPoints + 2;

// Douglas-Peucker on `points[0..count)`, count <= kMaxSimplifyPoints: marks the points to keep
// in `keep` (first and last always kept). Returns how many are kept. Iterative, fixed-size
// stack, no heap.
size_t SimplifyPolyline(const MetricPoint* points, size_t count, float tolerance_m, bool* keep);

}  // namespace sarathi::core
