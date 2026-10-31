// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// Indexing the caller's route storage; bounds come from count_.

#include "route_tracker.h"

#include <algorithm>
#include <cmath>

namespace sarathi::core {
namespace {

constexpr float kDegPerRad = 57.2957795F;
constexpr int kMaxSimplifyPasses = 12;

int16_t ToMetres(float v) {
  return static_cast<int16_t>(std::clamp(std::lround(v), -32767L, 32767L));
}

}  // namespace

void RouteTracker::Clear() {
  count_ = 0;
  maneuver_count_ = 0;
  has_fix_ = false;
}

void RouteTracker::Measure() {
  storage_.cumulative[0] = 0.0F;
  for (size_t i = 1; i < count_; ++i) {
    const MetricPoint a = storage_.points[i - 1];
    const MetricPoint b = storage_.points[i];
    storage_.cumulative[i] = storage_.cumulative[i - 1] + std::hypot(b.x - a.x, b.y - a.y);
  }
}

bool RouteTracker::AddManeuver(size_t vertex, const Maneuver& maneuver) {
  if (vertex >= count_ || maneuver_count_ >= maneuvers_.size() ||
      (maneuver_count_ > 0 && vertex <= maneuvers_.at(maneuver_count_ - 1).vertex)) {
    return false;
  }
  maneuvers_.at(maneuver_count_++) = RouteManeuver{static_cast<uint16_t>(vertex), maneuver};
  return true;
}

float RouteTracker::LengthM() const { return HasRoute() ? storage_.cumulative[count_ - 1] : 0.0F; }

size_t RouteTracker::SegmentAt(float along_m) const {
  // The last point whose distance is <= along_m, as a segment start.
  const float* begin = storage_.cumulative;
  const float* it = std::upper_bound(begin, begin + count_, along_m);
  const auto i = static_cast<size_t>(it - begin);
  return std::clamp<size_t>(i == 0 ? 0 : i - 1, 0, count_ - 2);
}

MetricPoint RouteTracker::PointAt(float along_m) const {
  along_m = std::clamp(along_m, 0.0F, LengthM());
  const size_t seg = SegmentAt(along_m);
  const float len = storage_.cumulative[seg + 1] - storage_.cumulative[seg];
  const float t = len > 0.0F ? (along_m - storage_.cumulative[seg]) / len : 0.0F;
  const MetricPoint a = storage_.points[seg];
  const MetricPoint b = storage_.points[seg + 1];
  return {a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t)};
}

uint16_t RouteTracker::HeadingAt(float along_m) const {
  if (!HasRoute()) {
    return 0;
  }
  const size_t seg = SegmentAt(std::clamp(along_m, 0.0F, LengthM()));
  const MetricPoint a = storage_.points[seg];
  const MetricPoint b = storage_.points[seg + 1];
  float deg = std::atan2(b.x - a.x, b.y - a.y) * kDegPerRad;  // 0 = north, 90 = east
  if (deg < 0.0F) {
    deg += 360.0F;
  }
  return static_cast<uint16_t>(std::lround(deg) % 360);
}

RouteFix RouteTracker::Nearest(MetricPoint rider, size_t first_segment, size_t last_segment) const {
  RouteFix best{0.0F, INFINITY};
  for (size_t i = first_segment; i <= last_segment; ++i) {
    const MetricPoint a = storage_.points[i];
    const MetricPoint b = storage_.points[i + 1];
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len2 = (dx * dx) + (dy * dy);
    const float t =
        len2 > 0.0F
            ? std::clamp((((rider.x - a.x) * dx) + ((rider.y - a.y) * dy)) / len2, 0.0F, 1.0F)
            : 0.0F;
    const float d = std::hypot(rider.x - (a.x + (t * dx)), rider.y - (a.y + (t * dy)));
    if (d < best.offset_m) {
      const float seg_len = storage_.cumulative[i + 1] - storage_.cumulative[i];
      best = RouteFix{storage_.cumulative[i] + (t * seg_len), d};
    }
  }
  return best;
}

RouteFix RouteTracker::Locate(MetricPoint rider) {
  if (!HasRoute()) {
    return RouteFix{};
  }
  RouteFix fix{0.0F, INFINITY};
  if (has_fix_) {
    fix = Nearest(rider, SegmentAt(last_fix_.along_m - kSearchBehindM),
                  SegmentAt(last_fix_.along_m + kSearchAheadM));
  }
  if (fix.offset_m > kOnRouteM) {
    fix = Nearest(rider, 0, count_ - 2);  // first fix, or off the route: search it all
  }
  has_fix_ = true;
  last_fix_ = fix;
  return fix;
}

void RouteTracker::Window(MetricPoint rider, const RouteFix& fix, uint16_t heading_deg,
                          float behind_m, float ahead_m, float tolerance_m,
                          RouteWindow& out) const {
  out.count = 0;
  out.has_maneuver_point = false;
  out.heading_deg = static_cast<uint16_t>(heading_deg % 360);
  if (!HasRoute()) {
    return;
  }
  const float start = std::max(0.0F, fix.along_m - behind_m);
  const float end = std::min(LengthM(), fix.along_m + ahead_m);

  // The vertices strictly between start and end, every `stride`-th if there are more than the
  // simplifier takes (it keeps the shape at screen scale either way).
  const size_t first = SegmentAt(start) + 1;
  size_t last = SegmentAt(end);
  if (storage_.cumulative[last] >= end && last > 0) {
    --last;
  }
  const size_t inside = last >= first ? last - first + 1 : 0;
  constexpr size_t kRoom = kMaxSimplifyPoints - 2;
  const size_t stride = inside > kRoom ? (inside + kRoom - 1) / kRoom : 1;

  std::array<MetricPoint, kMaxSimplifyPoints> input{};
  size_t n = 0;
  input.at(n++) = PointAt(start);
  for (size_t i = first; i <= last && inside > 0 && n < kMaxSimplifyPoints - 1; i += stride) {
    input.at(n++) = storage_.points[i];
  }
  input.at(n++) = PointAt(end);

  std::array<bool, kMaxSimplifyPoints> keep{};
  float tolerance = tolerance_m;
  size_t kept = SimplifyPolyline(input.data(), n, tolerance, keep.data());
  for (int pass = 0; kept > kMaxRoutePoints && pass < kMaxSimplifyPasses; ++pass) {
    tolerance *= 2.0F;
    kept = SimplifyPolyline(input.data(), n, tolerance, keep.data());
  }
  for (size_t i = 0; i < n && out.count < kMaxRoutePoints; ++i) {
    if (keep.at(i)) {
      out.points.at(out.count++) = {ToMetres(input.at(i).x - rider.x),
                                    ToMetres(input.at(i).y - rider.y)};
    }
  }
  for (size_t i = 0; i < maneuver_count_; ++i) {
    const RouteManeuver& m = maneuvers_.at(i);
    const float at = storage_.cumulative[m.vertex];
    if (at > fix.along_m) {
      if (at <= end) {
        const MetricPoint p = storage_.points[m.vertex];
        out.has_maneuver_point = true;
        out.maneuver_point = {ToMetres(p.x - rider.x), ToMetres(p.y - rider.y)};
      }
      break;
    }
  }
}

}  // namespace sarathi::core

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
