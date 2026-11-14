// Software rasteriser for the on-device map (ADR-0009 spike result): thick lines and filled
// polygons written straight into an RGB565 buffer, clipped to a clip rectangle. LVGL's own
// lv_draw_line costs grow with the square of the segment count in LVGL 9.1, so the map draws
// here instead. Colours are raw 16-bit values, already in the panel's order. Pure logic.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sarathi::core {

struct ScreenPoint {
  int32_t x = 0;
  int32_t y = 0;
};

struct ClipRect {
  int32_t x1 = 0;  // inclusive
  int32_t y1 = 0;
  int32_t x2 = 0;  // inclusive
  int32_t y2 = 0;
};

// A buffer covering screen pixels from (buf_x, buf_y), `stride` pixels per row. Drawing only
// touches pixels inside `clip`, which must lie inside the buffer.
struct Canvas {
  uint16_t* px = nullptr;
  int32_t stride = 0;
  int32_t buf_x = 0;
  int32_t buf_y = 0;
  ClipRect clip;
};

void FillRect(Canvas& canvas, const ClipRect& rect, uint16_t color);

// A line `width` pixels thick (at least 1) centred on the segment; the ends are square.
void DrawSegment(Canvas& canvas, ScreenPoint a, ScreenPoint b, int32_t width, uint16_t color);

// A dashed line: `dash` px on, `gap` px off, starting `phase` px into the pattern. Returns the
// phase at `b`, so a polyline's dashes run on across its vertices.
float DrawDashedSegment(Canvas& canvas, ScreenPoint a, ScreenPoint b, int32_t width, uint16_t color,
                        int32_t dash, int32_t gap, float phase);

// Short lines across a→b, `length` px long and `thickness` thick, every `spacing` px, starting
// `phase` px in. Returns the phase at `b`.
float DrawCrossTicks(Canvas& canvas, ScreenPoint a, ScreenPoint b, int32_t length,
                     int32_t thickness, uint16_t color, int32_t spacing, float phase);

// A filled square `width` pixels across, centred on `p`: covers the joints of thick polylines.
void DrawDot(Canvas& canvas, ScreenPoint p, int32_t width, uint16_t color);

// A ring like the UI's RingGauge (step G6.2: drawn with the map, it costs far less than LVGL's
// arc in every strip). Centred on (cx, cy) in pixel-centre coordinates (pixel x covers x to
// x + 1), `r_out` outer radius, `width` thick: the track all round, and the indicator clockwise
// from the top over `fraction` (0 to 1) with round ends. Only the ring's own pixels are visited.
struct Ring {
  float cx = 240;
  float cy = 240;
  float r_out = 235;
  float width = 10;
  uint16_t track = 0;
  uint16_t indicator = 0;
  float fraction = 0;
};
void DrawRing(Canvas& canvas, const Ring& ring);

// Even-odd scanline fill of a closed ring (the last point joins the first). Rings with more
// than kMaxFillCrossings crossings on one scanline fill only the first ones.
constexpr size_t kMaxFillCrossings = 64;
void FillPolygon(Canvas& canvas, const ScreenPoint* ring, size_t n, uint16_t color);

}  // namespace sarathi::core
