#include "map_render.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace sarathi::core {

namespace {

constexpr float kDegToRad = 3.14159265358979F / 180.0F;
constexpr float kOne = 65536.0F;
constexpr float kQLimit = 1 << 29;  // larger offsets could overflow; such chunks aren't seen
constexpr int32_t kMaxMargin = 32;  // px: the widest road plus glow, outside the circle

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

using detail::Affine;
using detail::ChunkView;
using detail::QuantaCircle;

bool AffineFor(const MapCamera& c, const Basis& basis, const StoredChunk& chunk, Affine* out) {
  const float inv = 1.0F / c.metres_per_px;
  const float q = static_cast<float>(chunk.quantum_dm) / 10.0F * inv;  // px per quantum
  const float dx = static_cast<float>(chunk.origin_x_m) - c.x_m;
  const float dy = static_cast<float>(chunk.origin_y_m) - c.y_m;
  const float bx = static_cast<float>(c.anchor_x) + (dx * basis.rx + dy * basis.ry) * inv;
  const float by = static_cast<float>(c.anchor_y) - (dx * basis.fx + dy * basis.fy) * inv;
  if (std::fabs(bx) * kOne > kQLimit || std::fabs(by) * kOne > kQLimit) return false;
  const auto q16 = [](float v) { return static_cast<int32_t>(std::lround(v * kOne)); };
  *out = Affine{q16(bx), q16(q * basis.rx),  q16(q * basis.ry),
                q16(by), q16(-q * basis.fx), q16(-q * basis.fy)};
  return true;
}

bool Sees(const QuantaCircle& v, int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y) {
  const int64_t dx = std::max<int64_t>({0, int64_t{min_x} - v.cx, int64_t{v.cx} - max_x});
  const int64_t dy = std::max<int64_t>({0, int64_t{min_y} - v.cy, int64_t{v.cy} - max_y});
  return dx * dx + dy * dy <= v.r2;
}

void DrawLine(Canvas& canvas, const ChunkView& v, const MapShape& s, int32_t width, uint16_t color,
              MapRenderStats& stats) {
  const MapPoint* pts = v.chunk.points + s.first;  // NOLINT(*-pointer-arithmetic)
  ScreenPoint prev = v.affine.Apply(pts[0]);
  for (uint32_t k = 1; k < s.count; ++k) {
    const ScreenPoint p = v.affine.Apply(pts[k]);  // NOLINT(*-pointer-arithmetic)
    if (p.x == prev.x && p.y == prev.y && k + 1 < s.count) continue;  // under a pixel
    DrawSegment(canvas, prev, p, width, color);
    if (width >= 4 && k + 1 < s.count) DrawDot(canvas, p, width, color);
    ++stats.segments;
    prev = p;
  }
}

}  // namespace

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

MapRenderStats MapRenderer::Render(ChunkStore& store, uint8_t level, const MapCamera& camera,
                                   const MapStyle& style, Canvas& canvas, uint32_t frame) {
  MapRenderStats stats;
  FillRect(canvas, canvas.clip, style.background);
  const Basis basis = BasisOf(camera);
  // The visible circle's centre in the ride frame, and its radius with a margin for line width.
  const float u = static_cast<float>(camera.view_cx - camera.anchor_x) * camera.metres_per_px;
  const float w = static_cast<float>(camera.anchor_y - camera.view_cy) * camera.metres_per_px;
  const float centre_x = camera.x_m + u * basis.rx + w * basis.fx;
  const float centre_y = camera.y_m + u * basis.ry + w * basis.fy;
  const float radius_m = static_cast<float>(camera.view_r + kMaxMargin) * camera.metres_per_px;
  // The clip area in the same terms: LVGL draws in strips, and most chunks miss a strip.
  const ScreenPoint clip_mid{(canvas.clip.x1 + canvas.clip.x2) / 2,
                             (canvas.clip.y1 + canvas.clip.y2) / 2};
  const float clip_half_w = static_cast<float>(canvas.clip.x2 - canvas.clip.x1 + 1) / 2.0F;
  const float clip_half_h = static_cast<float>(canvas.clip.y2 - canvas.clip.y1 + 1) / 2.0F;
  const float clip_r =
      (std::sqrt(clip_half_w * clip_half_w + clip_half_h * clip_half_h) + kMaxMargin) *
      camera.metres_per_px;
  const float cu = static_cast<float>(clip_mid.x - camera.anchor_x) * camera.metres_per_px;
  const float cw = static_cast<float>(camera.anchor_y - clip_mid.y) * camera.metres_per_px;
  const float clip_x = camera.x_m + cu * basis.rx + cw * basis.fx;
  const float clip_y = camera.y_m + cu * basis.ry + cw * basis.fy;
  // The smaller of the two circles is enough to cull with.
  const bool use_clip = clip_r < radius_m;
  const float cull_x = use_clip ? clip_x : centre_x;
  const float cull_y = use_clip ? clip_y : centre_y;
  const float cull_r = use_clip ? clip_r : radius_m;

  ChunkView* views = views_;
  size_t n_views = 0;
  for (size_t i = 0; i < store.Count() && n_views < kMaxVisible; ++i) {
    const StoredChunk c = store.At(i);
    if (c.id.level != level) continue;
    const float dx = std::max(
        {0.0F, static_cast<float>(c.box.min_x) - cull_x, cull_x - static_cast<float>(c.box.max_x)});
    const float dy = std::max(
        {0.0F, static_cast<float>(c.box.min_y) - cull_y, cull_y - static_cast<float>(c.box.max_y)});
    if (dx * dx + dy * dy > cull_r * cull_r) continue;
    ChunkView& v = views[n_views];
    if (!AffineFor(camera, basis, c, &v.affine)) continue;
    const float q_m = static_cast<float>(c.quantum_dm) / 10.0F;
    const float rq = cull_r / q_m;
    v.chunk = c;
    v.view = QuantaCircle{
        static_cast<int32_t>(std::lround((cull_x - static_cast<float>(c.origin_x_m)) / q_m)),
        static_cast<int32_t>(std::lround((cull_y - static_cast<float>(c.origin_y_m)) / q_m)),
        static_cast<int64_t>(rq * rq) + 1};
    store.MarkDrawn(i, frame);
    ++n_views;
  }
  stats.chunks = static_cast<uint32_t>(n_views);

  if (style.draw_polygons) {
    ScreenPoint* ring = ring_;
    for (size_t vi = 0; vi < n_views; ++vi) {
      const ChunkView& v = views[vi];
      for (uint16_t i = 0; i < v.chunk.n_polys; ++i) {
        const MapShape& s = v.chunk.polys[i];  // NOLINT(*-pointer-arithmetic)
        if (!Sees(v.view, s.min_x, s.min_y, s.max_x, s.max_y)) continue;
        const size_t n = std::min<size_t>(s.count, kMaxRing);
        for (size_t k = 0; k < n; ++k) {
          ring[k] = v.affine.Apply(v.chunk.points[s.first + k]);  // NOLINT(*-pointer-arithmetic)
        }
        FillPolygon(canvas, ring, n, s.kind == 0 ? style.water : style.park);
        ++stats.shapes;
      }
    }
  }

  const bool skip_service = camera.metres_per_px > style.service_max_metres_per_px;
  for (int pass = style.glow_extra_px > 0 ? 0 : 1; pass < 2; ++pass) {
    for (int cls = kRoadClasses - 1; cls >= 0; --cls) {
      if (cls == kRoadClasses - 1 && skip_service) continue;
      const int32_t width = style.road_px[cls] + (pass == 0 ? style.glow_extra_px : 0);
      const uint16_t color = pass == 0 ? style.glow[cls] : style.road[cls];
      for (size_t vi = 0; vi < n_views; ++vi) {
        const ChunkView& v = views[vi];
        for (uint16_t i = 0; i < v.chunk.n_lines; ++i) {
          const MapShape& s = v.chunk.lines[i];  // NOLINT(*-pointer-arithmetic)
          if (s.kind != cls || !Sees(v.view, s.min_x, s.min_y, s.max_x, s.max_y)) continue;
          DrawLine(canvas, v, s, width, color, stats);
          ++stats.shapes;
        }
      }
    }
  }
  return stats;
}

}  // namespace sarathi::core
