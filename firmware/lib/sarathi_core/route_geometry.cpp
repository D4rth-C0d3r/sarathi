// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// Indexing caller-provided tables is the point of this file; bounds come from the counts.

#include "route_geometry.h"

#include <algorithm>
#include <cmath>

namespace sarathi::core {
namespace {

constexpr float kPi = 3.14159265358979F;
constexpr float kDegPerRad = 180.0F / kPi;
// Window extraction can add an interpolated start and end point to the path vertices.
constexpr size_t kMaxWindowInput = kMaxPathPoints + 2;
constexpr int kMaxSimplifyPasses = 12;

float Distance(MetricPoint a, MetricPoint b) { return std::hypot(b.x - a.x, b.y - a.y); }

// Perpendicular distance from `p` to the segment a-b.
float SegmentDistance(MetricPoint p, MetricPoint a, MetricPoint b) {
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float len2 = (dx * dx) + (dy * dy);
  if (len2 <= 0.0F) {
    return Distance(p, a);
  }
  const float t = std::clamp((((p.x - a.x) * dx) + ((p.y - a.y) * dy)) / len2, 0.0F, 1.0F);
  return Distance(p, MetricPoint{a.x + (t * dx), a.y + (t * dy)});
}

int16_t ToMetres(float v) {
  return static_cast<int16_t>(std::clamp(std::lround(v), -32767L, 32767L));
}

}  // namespace

RoutePath::RoutePath(const MetricPoint* points, size_t point_count, const RouteManeuver* maneuvers,
                     size_t maneuver_count)
    : points_(points),
      count_(std::clamp<size_t>(point_count, 2, kMaxPathPoints)),
      maneuvers_(maneuvers),
      maneuver_count_(maneuver_count) {
  cumulative_[0] = 0.0F;
  for (size_t i = 1; i < count_; ++i) {
    cumulative_[i] = cumulative_[i - 1] + Distance(points_[i - 1], points_[i]);
  }
}

size_t RoutePath::SegmentAt(float s) const {
  // Last segment whose start is at or before s.
  size_t seg = 0;
  while (seg + 2 < count_ && cumulative_[seg + 1] <= s) {
    ++seg;
  }
  return seg;
}

MetricPoint RoutePath::PointAt(float s) const {
  s = std::clamp(s, 0.0F, LengthM());
  const size_t seg = SegmentAt(s);
  const float len = cumulative_[seg + 1] - cumulative_[seg];
  const float t = len > 0.0F ? (s - cumulative_[seg]) / len : 0.0F;
  const MetricPoint a = points_[seg];
  const MetricPoint b = points_[seg + 1];
  return {a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t)};
}

uint16_t RoutePath::HeadingAt(float s) const {
  const size_t seg = SegmentAt(std::clamp(s, 0.0F, LengthM()));
  const MetricPoint a = points_[seg];
  const MetricPoint b = points_[seg + 1];
  float deg = std::atan2(b.x - a.x, b.y - a.y) * kDegPerRad;  // 0 = north, 90 = east
  if (deg < 0.0F) {
    deg += 360.0F;
  }
  return static_cast<uint16_t>(std::lround(deg) % 360);
}

const RouteManeuver* RoutePath::NextManeuver(float s) const {
  for (size_t i = 0; i < maneuver_count_; ++i) {
    if (maneuvers_[i].vertex < count_ && cumulative_[maneuvers_[i].vertex] > s) {
      return &maneuvers_[i];
    }
  }
  return nullptr;
}

void RoutePath::Window(float s, float behind_m, float ahead_m, float tolerance_m,
                       RouteWindow& out) const {
  s = std::clamp(s, 0.0F, LengthM());
  const float start = std::max(0.0F, s - behind_m);
  const float end = std::min(LengthM(), s + ahead_m);
  const MetricPoint rider = PointAt(s);

  // Path from start to end: interpolated ends plus every vertex strictly between.
  std::array<MetricPoint, kMaxWindowInput> input{};
  size_t n = 0;
  input[n++] = PointAt(start);
  for (size_t i = 0; i < count_; ++i) {
    if (cumulative_[i] > start && cumulative_[i] < end) {
      input[n++] = points_[i];
    }
  }
  input[n++] = PointAt(end);

  // Simplify, loosening the tolerance until the result fits.
  std::array<bool, kMaxWindowInput> keep{};
  float tolerance = tolerance_m;
  size_t kept = SimplifyPolyline(input.data(), n, tolerance, keep.data());
  for (int pass = 0; kept > kMaxRoutePoints && pass < kMaxSimplifyPasses; ++pass) {
    tolerance *= 2.0F;
    kept = SimplifyPolyline(input.data(), n, tolerance, keep.data());
  }

  out.count = 0;
  for (size_t i = 0; i < n && out.count < kMaxRoutePoints; ++i) {
    if (keep[i]) {
      out.points[out.count++] = {ToMetres(input[i].x - rider.x), ToMetres(input[i].y - rider.y)};
    }
  }
  out.heading_deg = HeadingAt(s);

  out.has_maneuver_point = false;
  if (const RouteManeuver* next = NextManeuver(s);
      next != nullptr && cumulative_[next->vertex] <= end) {
    const MetricPoint m = points_[next->vertex];
    out.has_maneuver_point = true;
    out.maneuver_point = {ToMetres(m.x - rider.x), ToMetres(m.y - rider.y)};
  }
}

size_t SimplifyPolyline(const MetricPoint* points, size_t count, float tolerance_m, bool* keep) {
  if (count == 0) {
    return 0;
  }
  std::fill(keep, keep + count, false);
  keep[0] = true;
  keep[count - 1] = true;
  if (count <= 2) {
    return count;
  }

  struct Span {
    size_t first;
    size_t last;
  };
  // Each pop pushes at most two spans, so the stack depth stays below the point count.
  std::array<Span, kMaxWindowInput> stack{};
  size_t top = 0;
  stack[top++] = {0, count - 1};
  while (top > 0) {
    const Span span = stack[--top];
    float worst = -1.0F;
    size_t worst_index = span.first;
    for (size_t i = span.first + 1; i < span.last; ++i) {
      const float d = SegmentDistance(points[i], points[span.first], points[span.last]);
      if (d > worst) {
        worst = d;
        worst_index = i;
      }
    }
    if (worst > tolerance_m && top + 2 <= stack.size()) {
      keep[worst_index] = true;
      stack[top++] = {span.first, worst_index};
      stack[top++] = {worst_index, span.last};
    }
  }
  return static_cast<size_t>(std::count(keep, keep + count, true));
}

}  // namespace sarathi::core

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
