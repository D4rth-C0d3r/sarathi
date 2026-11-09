#include "map_render.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace sarathi::core {

namespace {

constexpr float kDegToRad = 3.14159265358979F / 180.0F;
constexpr float kOne = 65536.0F;
constexpr int32_t kHalf = 1 << 15;
constexpr float kQLimit = 1 << 29;    // larger offsets could overflow; such chunks aren't seen
constexpr int32_t kMaxMargin = 32;    // px: the widest road plus glow, outside the circle
constexpr uint8_t kQuarterLevel = 2;  // protocol 1.2: a z15 quarter of an L0 tile

struct Basis {
  float rx, ry;  // screen right, in the ride frame
  float fx, fy;  // screen up (the heading)
};

Basis BasisOf(const MapCamera& c) {
  const float h = c.heading_deg * kDegToRad;
  const float s = std::sin(h);
  const float co = std::cos(h);
  return Basis{co, -s, s, co};
}

// screen = b + p.x * a_x + p.y * a_y, Q16, for points in one chunk's quanta.
struct Affine {
  int32_t bx, axx, axy;
  int32_t by, ayx, ayy;

  [[nodiscard]] ScreenPoint Apply(const MapPoint& p) const {
    return ScreenPoint{(bx + p.x * axx + p.y * axy + kHalf) >> 16,
                       (by + p.x * ayx + p.y * ayy + kHalf) >> 16};
  }
};

bool AffineFor(const MapCamera& c, const Basis& basis, const StoredChunk& chunk, Affine* out) {
  const float inv = 1.0F / c.metres_per_px;
  const float q = static_cast<float>(chunk.quantum_dm) / 10.0F * inv;  // px per quantum
  const float dx = static_cast<float>(chunk.origin_x_m) - c.x_m;
  const float dy = static_cast<float>(chunk.origin_y_m) - c.y_m;
  const float bx = static_cast<float>(c.anchor_x) + (dx * basis.rx + dy * basis.ry) * inv;
  const float by = static_cast<float>(c.anchor_y) - (dx * basis.fx + dy * basis.fy) * inv;
  if (std::fabs(bx) * kOne > kQLimit || std::fabs(by) * kOne > kQLimit) {
    return false;
  }
  const auto q16 = [](float v) { return static_cast<int32_t>(std::lround(v * kOne)); };
  *out = Affine{q16(bx), q16(q * basis.rx),  q16(q * basis.ry),
                q16(by), q16(-q * basis.fx), q16(-q * basis.fy)};
  return true;
}

// The visible circle in one chunk's quanta, for box tests in integers.
struct QuantaCircle {
  int32_t cx, cy;
  int64_t r2;

  [[nodiscard]] bool Sees(const MapShape& s) const {
    const int64_t dx = std::max<int64_t>({0, int64_t{s.min_x} - cx, int64_t{cx} - s.max_x});
    const int64_t dy = std::max<int64_t>({0, int64_t{s.min_y} - cy, int64_t{cy} - s.max_y});
    return dx * dx + dy * dy <= r2;
  }
};

bool Crosses(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y, int32_t margin,
             const ClipRect& clip) {
  return max_x + margin >= clip.x1 && min_x - margin <= clip.x2 && max_y + margin >= clip.y1 &&
         min_y - margin <= clip.y2;
}

}  // namespace

struct MapRenderer::Seen {
  StoredChunk chunk;
  Affine affine;
  QuantaCircle view;
};

ScreenPoint ProjectMetres(const MapCamera& camera, float x_m, float y_m) {
  const Basis b = BasisOf(camera);
  const float dx = x_m - camera.x_m;
  const float dy = y_m - camera.y_m;
  const float inv = 1.0F / camera.metres_per_px;
  return ScreenPoint{static_cast<int32_t>(std::lround(static_cast<float>(camera.anchor_x) +
                                                      (dx * b.rx + dy * b.ry) * inv)),
                     static_cast<int32_t>(std::lround(static_cast<float>(camera.anchor_y) -
                                                      (dx * b.fx + dy * b.fy) * inv))};
}

void MapRenderer::Prepare(ChunkStore* store, uint8_t level, const MapCamera& camera,
                          const MapStyle& style, uint32_t frame) {
  const uint32_t start = time_us_ != nullptr ? time_us_() : 0;
  style_ = style;
  // A new frame's counts; the draw times keep adding up until TakeStats().
  MapRenderStats fresh;
  fresh.background_us = stats_.background_us;
  fresh.polygons_us = stats_.polygons_us;
  fresh.lines_us = stats_.lines_us;
  fresh.prepare_us = stats_.prepare_us;
  stats_ = fresh;
  n_shapes_ = 0;
  n_points_ = 0;
  const Basis basis = BasisOf(camera);
  // The visible circle's centre in the ride frame, and its radius with a margin for line width.
  const float u = static_cast<float>(camera.view_cx - camera.anchor_x) * camera.metres_per_px;
  const float w = static_cast<float>(camera.anchor_y - camera.view_cy) * camera.metres_per_px;
  const float cx = camera.x_m + u * basis.rx + w * basis.fx;
  const float cy = camera.y_m + u * basis.ry + w * basis.fy;
  const float r = static_cast<float>(camera.view_r + kMaxMargin) * camera.metres_per_px;

  // UI task only; static keeps it off the stack.
  static Seen
      seen[kMaxVisibleChunks];  // NOLINT(*-avoid-c-arrays,*-avoid-non-const-global-variables)
  size_t n_seen = 0;
  const size_t n_stored = store == nullptr ? 0 : store->Count();
  for (size_t i = 0; i < n_stored && n_seen < kMaxVisibleChunks; ++i) {
    const StoredChunk c = store->At(i);
    const bool quarter_of_l0 = level == 0 && c.id.level == kQuarterLevel;
    if (c.id.level != level && !quarter_of_l0) {
      continue;
    }
    const float dx = std::max(
        {0.0F, static_cast<float>(c.box.min_x) - cx, cx - static_cast<float>(c.box.max_x)});
    const float dy = std::max(
        {0.0F, static_cast<float>(c.box.min_y) - cy, cy - static_cast<float>(c.box.max_y)});
    if (dx * dx + dy * dy > r * r) {
      continue;
    }
    Seen& s = seen[n_seen];  // NOLINT(*-constant-array-index)
    if (!AffineFor(camera, basis, c, &s.affine)) {
      continue;
    }
    const float q_m = static_cast<float>(c.quantum_dm) / 10.0F;
    const float rq = r / q_m;
    s.chunk = c;
    s.view = QuantaCircle{
        static_cast<int32_t>(std::lround((cx - static_cast<float>(c.origin_x_m)) / q_m)),
        static_cast<int32_t>(std::lround((cy - static_cast<float>(c.origin_y_m)) / q_m)),
        static_cast<int64_t>(rq * rq) + 1};
    store->MarkDrawn(i, frame);
    ++n_seen;
  }
  stats_.chunks = static_cast<uint32_t>(n_seen);

  // Cache in order of importance (motorway first, polygons last), so a full cache drops the
  // least important; Draw() walks it backwards, minor first.
  const auto add = [&](const Seen& s, const MapShape& shape, bool polygon) {
    if (n_shapes_ == kMaxShapes || n_points_ + shape.count > kMaxPoints) {
      stats_.truncated = true;
      return;
    }
    Shape out{INT32_MAX, INT32_MAX,  INT32_MIN, INT32_MIN, static_cast<uint32_t>(n_points_),
              0,         shape.kind, polygon};
    const MapPoint* pts = s.chunk.points + shape.first;  // NOLINT(*-pointer-arithmetic)
    for (uint32_t k = 0; k < shape.count; ++k) {
      const ScreenPoint p = s.affine.Apply(pts[k]);  // NOLINT(*-pointer-arithmetic)
      const bool last = k + 1 == shape.count;
      if (out.count > 0 && !last) {
        const ScreenPoint& prev = points_[n_points_ - 1];  // NOLINT(*-constant-array-index)
        if (p.x == prev.x && p.y == prev.y) {
          continue;  // under a pixel
        }
      }
      points_[n_points_++] = p;  // NOLINT(*-constant-array-index)
      ++out.count;
      out.min_x = std::min(out.min_x, p.x);
      out.min_y = std::min(out.min_y, p.y);
      out.max_x = std::max(out.max_x, p.x);
      out.max_y = std::max(out.max_y, p.y);
    }
    if (out.count < (polygon ? 3 : 2)) {
      n_points_ -= out.count;
      return;
    }
    shapes_[n_shapes_++] = out;  // NOLINT(*-constant-array-index)
    stats_.segments += polygon ? 0 : out.count - 1;
  };
  const bool skip_service = camera.metres_per_px > style.service_max_metres_per_px;
  for (int cls = 0; cls < kRoadClasses; ++cls) {
    if (cls == kRoadClasses - 1 && skip_service) {
      continue;
    }
    for (size_t i = 0; i < n_seen; ++i) {
      const Seen& s = seen[i];  // NOLINT(*-constant-array-index)
      for (uint16_t k = 0; k < s.chunk.n_lines; ++k) {
        const MapShape& shape = s.chunk.lines[k];  // NOLINT(*-pointer-arithmetic)
        if (shape.kind == cls && s.view.Sees(shape)) {
          add(s, shape, false);
        }
      }
    }
  }
  if (style.draw_polygons) {
    for (size_t i = 0; i < n_seen; ++i) {
      const Seen& s = seen[i];  // NOLINT(*-constant-array-index)
      for (uint16_t k = 0; k < s.chunk.n_polys; ++k) {
        const MapShape& shape = s.chunk.polys[k];  // NOLINT(*-pointer-arithmetic)
        if (s.view.Sees(shape)) {
          add(s, shape, true);
        }
      }
    }
  }
  stats_.shapes = static_cast<uint32_t>(n_shapes_);
  n_route_ = std::min(route_.count, kMaxRoutePoints);
  for (size_t i = 0; i < n_route_; ++i) {
    const RoutePoint& p = route_.points[i];  // NOLINT(*-pointer-arithmetic)
    route_points_[i] = ProjectMetres(camera, camera.x_m + static_cast<float>(p.x_m),  // NOLINT
                                     camera.y_m + static_cast<float>(p.y_m));
  }
  stats_.prepare_us += time_us_ != nullptr ? time_us_() - start : 0;
}

void MapRenderer::Draw(Canvas& canvas) {
  const auto now = [this] { return time_us_ != nullptr ? time_us_() : 0; };
  uint32_t mark = now();
  FillRect(canvas, canvas.clip, style_.background);
  stats_.background_us += now() - mark;

  mark = now();
  for (size_t i = n_shapes_; i-- > 0;) {
    const Shape& s = shapes_[i];  // NOLINT(*-constant-array-index)
    if (!s.polygon) {
      break;  // polygons come last in the cache
    }
    if (Crosses(s.min_x, s.min_y, s.max_x, s.max_y, 0, canvas.clip)) {
      FillPolygon(canvas, points_ + s.first, s.count,  // NOLINT(*-pointer-arithmetic)
                  s.kind == 0 ? style_.water : style_.park);
    }
  }
  stats_.polygons_us += now() - mark;

  mark = now();
  for (int pass = style_.glow_extra_px > 0 ? 0 : 1; pass < 2; ++pass) {
    for (size_t i = n_shapes_; i-- > 0;) {
      const Shape& s = shapes_[i];  // NOLINT(*-constant-array-index)
      if (s.polygon) {
        continue;
      }
      // NOLINTNEXTLINE(*-constant-array-index)
      const int32_t width = style_.road_px[s.kind] + (pass == 0 ? style_.glow_extra_px : 0);
      if (!Crosses(s.min_x, s.min_y, s.max_x, s.max_y, width, canvas.clip)) {
        continue;
      }
      // NOLINTNEXTLINE(*-constant-array-index)
      const uint16_t color = pass == 0 ? style_.glow[s.kind] : style_.road[s.kind];
      const ScreenPoint* p = points_ + s.first;  // NOLINT(*-pointer-arithmetic)
      for (uint32_t k = 1; k < s.count; ++k) {
        // NOLINTBEGIN(*-pointer-arithmetic)
        const ScreenPoint& a = p[k - 1];
        const ScreenPoint& b = p[k];
        // NOLINTEND(*-pointer-arithmetic)
        if (!Crosses(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y),
                     width, canvas.clip)) {
          continue;
        }
        DrawSegment(canvas, a, b, width, color);
        if (width >= 4 && k + 1 < s.count) {
          DrawDot(canvas, b, width, color);
        }
      }
    }
  }
  // The route over the roads: wider than any road, joints filled.
  for (size_t i = 1; i < n_route_; ++i) {
    // NOLINTBEGIN(*-constant-array-index)
    DrawSegment(canvas, route_points_[i - 1], route_points_[i], route_.width_px, route_.color);
    if (i + 1 < n_route_) {
      DrawDot(canvas, route_points_[i], route_.width_px, route_.color);
    }
    // NOLINTEND(*-constant-array-index)
  }
  stats_.lines_us += now() - mark;
}

MapRenderStats MapRenderer::Render(ChunkStore& store, uint8_t level, const MapCamera& camera,
                                   const MapStyle& style, Canvas& canvas, uint32_t frame) {
  Prepare(&store, level, camera, style, frame);
  Draw(canvas);
  return TakeStats();
}

MapRenderStats MapRenderer::TakeStats() {
  const MapRenderStats s = stats_;
  stats_.prepare_us = 0;
  stats_.background_us = 0;
  stats_.polygons_us = 0;
  stats_.lines_us = 0;
  return s;
}

}  // namespace sarathi::core
