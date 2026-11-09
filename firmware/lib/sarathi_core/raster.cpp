#include "raster.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace sarathi::core {

namespace {

// NOLINTBEGIN(*-pointer-arithmetic): pixel rows are addressed by offset.

void VSpan(Canvas& c, int32_t x, int32_t y1, int32_t y2, uint16_t color) {
  if (x < c.clip.x1 || x > c.clip.x2) return;
  y1 = std::max(y1, c.clip.y1);
  y2 = std::min(y2, c.clip.y2);
  uint16_t* p = c.px + (y1 - c.buf_y) * c.stride + (x - c.buf_x);
  for (int32_t y = y1; y <= y2; ++y, p += c.stride) *p = color;
}

void HSpan(Canvas& c, int32_t y, int32_t x1, int32_t x2, uint16_t color) {
  if (y < c.clip.y1 || y > c.clip.y2) return;
  x1 = std::max(x1, c.clip.x1);
  x2 = std::min(x2, c.clip.x2);
  if (x1 > x2) return;
  uint16_t* p = c.px + (y - c.buf_y) * c.stride + (x1 - c.buf_x);
  std::fill(p, p + (x2 - x1 + 1), color);
}

// NOLINTEND(*-pointer-arithmetic)

enum : uint8_t { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8 };

uint8_t Outcode(int64_t x, int64_t y, const ClipRect& r) {
  uint8_t code = 0;
  if (x < r.x1) code |= kLeft;
  if (x > r.x2) code |= kRight;
  if (y < r.y1) code |= kTop;
  if (y > r.y2) code |= kBottom;
  return code;
}

// Cohen-Sutherland in 64 bits; false if the segment misses the rectangle.
bool ClipSegment(int64_t& x0, int64_t& y0, int64_t& x1, int64_t& y1, const ClipRect& r) {
  uint8_t c0 = Outcode(x0, y0, r);
  uint8_t c1 = Outcode(x1, y1, r);
  for (int guard = 0; guard < 8; ++guard) {
    if ((c0 | c1) == 0) return true;
    if ((c0 & c1) != 0) return false;
    const uint8_t out = c0 != 0 ? c0 : c1;
    int64_t x = 0;
    int64_t y = 0;
    if ((out & kBottom) != 0) {
      x = x0 + (x1 - x0) * (r.y2 - y0) / (y1 - y0);
      y = r.y2;
    } else if ((out & kTop) != 0) {
      x = x0 + (x1 - x0) * (r.y1 - y0) / (y1 - y0);
      y = r.y1;
    } else if ((out & kRight) != 0) {
      y = y0 + (y1 - y0) * (r.x2 - x0) / (x1 - x0);
      x = r.x2;
    } else {
      y = y0 + (y1 - y0) * (r.x1 - x0) / (x1 - x0);
      x = r.x1;
    }
    if (out == c0) {
      x0 = x;
      y0 = y;
      c0 = Outcode(x0, y0, r);
    } else {
      x1 = x;
      y1 = y;
      c1 = Outcode(x1, y1, r);
    }
  }
  return false;
}

}  // namespace

void FillRect(Canvas& canvas, const ClipRect& rect, uint16_t color) {
  const int32_t y1 = std::max(rect.y1, canvas.clip.y1);
  const int32_t y2 = std::min(rect.y2, canvas.clip.y2);
  for (int32_t y = y1; y <= y2; ++y) HSpan(canvas, y, rect.x1, rect.x2, color);
}

void DrawSegment(Canvas& canvas, ScreenPoint a, ScreenPoint b, int32_t width, uint16_t color) {
  width = std::max<int32_t>(width, 1);
  const int64_t adx = std::abs(int64_t{b.x} - a.x);
  const int64_t ady = std::abs(int64_t{b.y} - a.y);
  const bool x_major = adx >= ady;
  // The span across the line, so diagonal lines keep their width: width * length / major axis.
  int32_t span = width;
  if (width > 1 && adx != 0 && ady != 0) {
    const float len = std::sqrt(static_cast<float>(adx) * static_cast<float>(adx) +
                                static_cast<float>(ady) * static_cast<float>(ady));
    const auto major = static_cast<float>(std::max(adx, ady));
    span = static_cast<int32_t>(static_cast<float>(width) * len / major + 0.5F);
  }
  const int32_t before = (span - 1) / 2;
  const int32_t after = span - 1 - before;
  // Clipping only decides which columns (or rows) to visit; every pixel comes from the original
  // endpoints, so a line drawn in strips matches the line drawn whole.
  const ClipRect grown{canvas.clip.x1 - span, canvas.clip.y1 - span, canvas.clip.x2 + span,
                       canvas.clip.y2 + span};
  int64_t cx0 = a.x;
  int64_t cy0 = a.y;
  int64_t cx1 = b.x;
  int64_t cy1 = b.y;
  if (!ClipSegment(cx0, cy0, cx1, cy1, grown)) return;
  // Walk the major axis from low to high: m is the major coordinate, n the minor one.
  int64_t m0 = x_major ? a.x : a.y;
  int64_t n0 = x_major ? a.y : a.x;
  int64_t m1 = x_major ? b.x : b.y;
  int64_t n1 = x_major ? b.y : b.x;
  if (m1 < m0) {
    std::swap(m0, m1);
    std::swap(n0, n1);
  }
  int64_t lo = x_major ? std::min(cx0, cx1) : std::min(cy0, cy1);
  int64_t hi = x_major ? std::max(cx0, cx1) : std::max(cy0, cy1);
  // Integer clip points are rounded towards the line's start: widen by one, then bound.
  lo = std::max(lo - 1, m0);
  hi = std::min(hi + 1, m1);
  const int64_t dm = m1 - m0;
  const int64_t dn = n1 - n0;
  if (dm == 0) {  // a single point
    if (x_major) {
      VSpan(canvas, static_cast<int32_t>(m0), static_cast<int32_t>(n0) - before,
            static_cast<int32_t>(n0) + after, color);
    } else {
      HSpan(canvas, static_cast<int32_t>(m0), static_cast<int32_t>(n0) - before,
            static_cast<int32_t>(n0) + after, color);
    }
    return;
  }
  // n(m) = n0 + floor((2 * dn * (m - m0) + dm) / (2 * dm)): exact, then stepped incrementally.
  const int64_t den = 2 * dm;
  const int64_t t = 2 * dn * (lo - m0) + dm;
  int64_t q = t / den;
  int64_t r = t % den;
  if (r < 0) {
    r += den;
    --q;
  }
  const int64_t step = 2 * dn;  // |step| <= den, so one correction per step at most
  for (int64_t m = lo; m <= hi; ++m) {
    const auto n = static_cast<int32_t>(n0 + q);
    if (x_major) {
      VSpan(canvas, static_cast<int32_t>(m), n - before, n + after, color);
    } else {
      HSpan(canvas, static_cast<int32_t>(m), n - before, n + after, color);
    }
    r += step;
    if (r >= den) {
      r -= den;
      ++q;
    } else if (r < 0) {
      r += den;
      --q;
    }
  }
}

void DrawDot(Canvas& canvas, ScreenPoint p, int32_t width, uint16_t color) {
  const int32_t before = (width - 1) / 2;
  const int32_t after = width - 1 - before;
  FillRect(canvas, ClipRect{p.x - before, p.y - before, p.x + after, p.y + after}, color);
}

void FillPolygon(Canvas& canvas, const ScreenPoint* ring, size_t n, uint16_t color) {
  if (n < 3) return;
  // NOLINTBEGIN(*-pointer-arithmetic)
  int32_t top = ring[0].y;
  int32_t bottom = ring[0].y;
  for (size_t i = 1; i < n; ++i) {
    top = std::min(top, ring[i].y);
    bottom = std::max(bottom, ring[i].y);
  }
  top = std::max(top, canvas.clip.y1);
  bottom = std::min(bottom, canvas.clip.y2);
  int32_t crossings[kMaxFillCrossings];
  for (int32_t y = top; y <= bottom; ++y) {
    // Sample at the pixel centre: an edge crosses if one end is above it and the other not.
    const int64_t yc2 = 2 * int64_t{y} + 1;  // 2 * (y + 0.5)
    size_t count = 0;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
      const int64_t ya2 = 2 * int64_t{ring[i].y};
      const int64_t yb2 = 2 * int64_t{ring[j].y};
      if ((ya2 < yc2) == (yb2 < yc2)) continue;
      if (count == kMaxFillCrossings) break;
      // x at yc, rounded: xa + (xb - xa) * (yc - ya) / (yb - ya), all doubled.
      const int64_t num = (int64_t{ring[j].x} - ring[i].x) * (yc2 - ya2);
      crossings[count++] = static_cast<int32_t>(ring[i].x + num / (yb2 - ya2));
    }
    std::sort(crossings, crossings + count);
    for (size_t k = 0; k + 1 < count; k += 2) {
      HSpan(canvas, y, crossings[k], crossings[k + 1] - 1, color);
    }
  }
  // NOLINTEND(*-pointer-arithmetic)
}

}  // namespace sarathi::core
