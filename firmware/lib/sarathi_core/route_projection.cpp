#include "route_projection.h"

#include <algorithm>
#include <cmath>

namespace sarathi::core {
namespace {

constexpr float kDegToRad = 3.14159265358979F / 180.0F;

struct Vec {
  float x = 0.0F;
  float y = 0.0F;
};

struct Transform {
  float cos_h = 1.0F;
  float sin_h = 0.0F;
  float scale = 1.0F;  // px per metre
  Vec rider{};

  // Metres (east, north) relative to the rider -> screen pixels, heading-up.
  [[nodiscard]] Vec Apply(float east, float north) const {
    const float right = (east * cos_h) - (north * sin_h);
    const float ahead = (east * sin_h) + (north * cos_h);
    return {rider.x + (right * scale), rider.y - (ahead * scale)};
  }
};

ScreenPoint ToScreen(Vec v) {
  return {static_cast<int16_t>(std::lround(v.x)), static_cast<int16_t>(std::lround(v.y))};
}

// Clips a-b to the circle. Returns false if no part of it is inside.
bool ClipToCircle(Vec& a, Vec& b, Vec c, float r) {
  const Vec d{b.x - a.x, b.y - a.y};
  const Vec f{a.x - c.x, a.y - c.y};
  const float qa = (d.x * d.x) + (d.y * d.y);
  const float qb = 2.0F * ((f.x * d.x) + (f.y * d.y));
  const float qc = (f.x * f.x) + (f.y * f.y) - (r * r);
  if (qa <= 0.0F) {
    return qc <= 0.0F;  // a single point
  }
  const float disc = (qb * qb) - (4.0F * qa * qc);
  if (disc < 0.0F) {
    return false;  // the line misses the circle
  }
  const float root = std::sqrt(disc);
  const float t0 = std::max(0.0F, (-qb - root) / (2.0F * qa));
  const float t1 = std::min(1.0F, (-qb + root) / (2.0F * qa));
  if (t0 > t1) {
    return false;  // the segment lies outside, before or after the circle
  }
  const Vec start{a.x + (d.x * t0), a.y + (d.y * t0)};
  b = {a.x + (d.x * t1), a.y + (d.y * t1)};
  a = start;
  return true;
}

bool Inside(Vec p, Vec c, float r) {
  const float dx = p.x - c.x;
  const float dy = p.y - c.y;
  return (dx * dx) + (dy * dy) <= r * r;
}

// Index of the segment closest to the rider (origin), and where on it (0..1).
void ClosestToRider(const RouteWindow& w, size_t& segment, float& t) {
  float best = -1.0F;
  segment = 0;
  t = 0.0F;
  for (size_t i = 0; i + 1 < w.count; ++i) {
    const Vec a{static_cast<float>(w.points[i].x_m), static_cast<float>(w.points[i].y_m)};
    const Vec b{static_cast<float>(w.points[i + 1].x_m), static_cast<float>(w.points[i + 1].y_m)};
    const Vec d{b.x - a.x, b.y - a.y};
    const float len2 = (d.x * d.x) + (d.y * d.y);
    const float u =
        len2 > 0.0F ? std::clamp(-((a.x * d.x) + (a.y * d.y)) / len2, 0.0F, 1.0F) : 0.0F;
    const Vec p{a.x + (d.x * u), a.y + (d.y * u)};
    const float dist = (p.x * p.x) + (p.y * p.y);
    if (best < 0.0F || dist < best) {
      best = dist;
      segment = i;
      t = u;
    }
  }
}

}  // namespace

void ProjectRoute(const RouteWindow& window, const ProjectionParams& params, ProjectedRoute& out) {
  out.count = 0;
  out.has_maneuver = false;
  const float h = static_cast<float>(window.heading_deg) * kDegToRad;
  const Transform tf{std::cos(h), std::sin(h), 1.0F / params.metres_per_px,
                     Vec{static_cast<float>(params.rider.x), static_cast<float>(params.rider.y)}};
  const Vec center{static_cast<float>(params.circle_center.x),
                   static_cast<float>(params.circle_center.y)};
  const float r = params.circle_radius_px;

  constexpr float kMinSegmentPx = 0.5F;  // drop zero-length pieces (rider on a vertex)
  auto emit = [&](Vec a, Vec b, bool behind) {
    if (std::hypot(b.x - a.x, b.y - a.y) < kMinSegmentPx) {
      return;
    }
    if (out.count < out.segments.size() && ClipToCircle(a, b, center, r)) {
      out.segments[out.count++] = ScreenSegment{ToScreen(a), ToScreen(b), behind};
    }
  };

  if (window.count >= 2) {
    size_t rider_segment = 0;
    float rider_t = 0.0F;
    ClosestToRider(window, rider_segment, rider_t);
    for (size_t i = 0; i + 1 < window.count; ++i) {
      const RoutePoint& p0 = window.points[i];
      const RoutePoint& p1 = window.points[i + 1];
      const Vec a = tf.Apply(static_cast<float>(p0.x_m), static_cast<float>(p0.y_m));
      const Vec b = tf.Apply(static_cast<float>(p1.x_m), static_cast<float>(p1.y_m));
      if (i < rider_segment) {
        emit(a, b, true);
      } else if (i > rider_segment) {
        emit(a, b, false);
      } else {  // split at the rider
        const Vec split{a.x + ((b.x - a.x) * rider_t), a.y + ((b.y - a.y) * rider_t)};
        emit(a, split, true);
        emit(split, b, false);
      }
    }
  }

  if (window.has_maneuver_point) {
    const Vec m = tf.Apply(static_cast<float>(window.maneuver_point.x_m),
                           static_cast<float>(window.maneuver_point.y_m));
    if (Inside(m, center, r)) {
      out.has_maneuver = true;
      out.maneuver = ToScreen(m);
    }
  }
}

}  // namespace sarathi::core
