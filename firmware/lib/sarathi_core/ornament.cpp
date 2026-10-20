#include "ornament.h"

#include <algorithm>
#include <cmath>

namespace sarathi::core {
namespace {

class Builder {
 public:
  explicit Builder(OrnamentShapes& shapes) : shapes_(&shapes) {}

  // Adds a line on the right and its mirror image on the left.
  void MirroredLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t width,
                    OrnamentColor color) {
    Line({x1, y1, x2, y2, width, color});
    Line({static_cast<int16_t>(-x1), y1, static_cast<int16_t>(-x2), y2, width, color});
  }

  void Line(const OrnamentLine& line) {
    if (shapes_->line_count < shapes_->lines.size()) {
      shapes_->lines.at(shapes_->line_count++) = line;
    }
  }

  void Rect(const OrnamentRect& rect) {
    if (shapes_->rect_count < shapes_->rects.size()) {
      shapes_->rects.at(shapes_->rect_count++) = rect;
    }
  }

 private:
  OrnamentShapes* shapes_;
};

// The rightmost pixel column inside the zone's outer circle for rows y0..y1.
int16_t ZoneRightEdge(int16_t y0, int16_t y1) {
  const float y = static_cast<float>(std::max(std::abs(y0), std::abs(y1)));
  const float r = static_cast<float>(kOrnamentMaxRadius);
  return static_cast<int16_t>(std::floor(std::sqrt((r * r) - (y * y))));
}

// Fighter jet HUD: a pitch-ladder scale on each side: a thin spine, short rungs and a longer
// accent rung at the horizon.
void BuildHud(Builder& b) {
  constexpr uint8_t kThin = 2;
  constexpr uint8_t kHorizon = 3;
  constexpr int16_t kSpineX = 197;
  constexpr int16_t kSpineHalf = 70;
  b.MirroredLine(kSpineX, -kSpineHalf, kSpineX, kSpineHalf, kThin, OrnamentColor::kDim);
  for (const int16_t y : {int16_t{-40}, int16_t{-20}, int16_t{20}, int16_t{40}}) {
    b.MirroredLine(kSpineX, y, 205, y, kThin, OrnamentColor::kDim);
  }
  b.MirroredLine(kSpineX, -60, 203, -60, kThin, OrnamentColor::kDim);
  b.MirroredLine(kSpineX, 60, 203, 60, kThin, OrnamentColor::kDim);
  b.MirroredLine(kSpineX, 0, 212, 0, kHorizon, OrnamentColor::kAccent);
}

// Racing game: a segmented tachometer bar on the right (redline at the top) and a chequered
// strip on the left.
void BuildRacing(Builder& b) {
  constexpr int16_t kInnerX = 198;
  constexpr int16_t kSegmentH = 6;
  constexpr int16_t kSegmentPitch = 10;
  constexpr int kSegments = 13;
  constexpr int kRedline = 3;  // top segments
  for (int i = 0; i < kSegments; ++i) {
    const auto y = static_cast<int16_t>(60 - (i * kSegmentPitch));  // bottom segment first
    const int16_t right = ZoneRightEdge(y, static_cast<int16_t>(y + kSegmentH - 1));
    const auto w = static_cast<int16_t>(std::min(4 + i, right - kInnerX + 1));
    const OrnamentColor color =
        i >= kSegments - kRedline ? OrnamentColor::kDanger : OrnamentColor::kSupporting;
    b.Rect({kInnerX, y, w, kSegmentH, color});
  }

  constexpr int16_t kSquare = 6;
  constexpr int16_t kTop = -42;
  constexpr int kRows = 14;
  for (int row = 0; row < kRows; ++row) {
    const auto y = static_cast<int16_t>(kTop + (row * kSquare));
    // Two columns; the white square alternates between them, row by row.
    const auto x =
        static_cast<int16_t>(row % 2 == 0 ? -kInnerX - kSquare + 1 : -kInnerX - (2 * kSquare) + 1);
    b.Rect({x, y, kSquare, kSquare, OrnamentColor::kPrimary});
  }
}

bool PointInZone(float x, float y, float half_width) {
  const float r = std::sqrt((x * x) + (y * y));
  return std::abs(x) - half_width >= static_cast<float>(kOrnamentMinAbsX) &&
         r + half_width <= static_cast<float>(kOrnamentMaxRadius);
}

}  // namespace

OrnamentShapes BuildOrnament(Ornament ornament) {
  OrnamentShapes shapes;
  Builder builder(shapes);
  switch (ornament) {
    case Ornament::kHudReticle:
      BuildHud(builder);
      break;
    case Ornament::kRacingTach:
      BuildRacing(builder);
      break;
    case Ornament::kNone:
      break;
  }
  return shapes;
}

// Each side of the zone is convex, so a straight stroke is inside if both ends are, on the
// same side.
bool InOrnamentZone(const OrnamentLine& line) {
  const float half = static_cast<float>(line.width) / 2.0F;
  return (line.x1 > 0) == (line.x2 > 0) && PointInZone(line.x1, line.y1, half) &&
         PointInZone(line.x2, line.y2, half);
}

bool InOrnamentZone(const OrnamentRect& rect) {
  if (rect.w <= 0 || rect.h <= 0) {
    return false;
  }
  const auto left = static_cast<float>(rect.x);
  const auto right = static_cast<float>(rect.x + rect.w - 1);
  const auto top = static_cast<float>(rect.y);
  const auto bottom = static_cast<float>(rect.y + rect.h - 1);
  return (rect.x > 0) == (rect.x + rect.w - 1 > 0) && PointInZone(left, top, 0.0F) &&
         PointInZone(right, top, 0.0F) && PointInZone(left, bottom, 0.0F) &&
         PointInZone(right, bottom, 0.0F);
}

Rgb OrnamentRgb(const ThemePalette& palette, OrnamentColor color) {
  switch (color) {
    case OrnamentColor::kAccent:
      return palette.accent;
    case OrnamentColor::kPrimary:
      return palette.text_primary;
    case OrnamentColor::kSupporting:
      return palette.text_supporting;
    case OrnamentColor::kDim:
      return palette.route_behind;
    case OrnamentColor::kDanger:
      return palette.danger;
  }
  return palette.accent;
}

}  // namespace sarathi::core
