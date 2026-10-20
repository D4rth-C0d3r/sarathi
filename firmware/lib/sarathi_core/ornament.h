// Theme ornaments as vector shapes (ADR-0003): plain data, so the zone rule runs as a native
// test. sarathi_ui draws them behind everything on every screen.
//
// Ornaments live only in the ornament zone: two side strips outside the widest text column
// (|x| >= kOrnamentMinAbsX) and inside the ring gauge (r <= kOrnamentMaxRadius). No screen
// puts anything there, so an ornament can never overlap primary content. They are static:
// nothing moves or flashes.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "theme.h"

namespace sarathi::core {

// Coordinates are relative to the screen centre, y down.
constexpr int16_t kOrnamentMinAbsX = 195;    // half the 380 px text column, plus a margin
constexpr int16_t kOrnamentMaxRadius = 214;  // inside the ring gauge (470 px across)
// The ring gauge's outer radius is 235 px; thicker than this, it would reach into the zone.
constexpr uint8_t kOrnamentMaxRingWidthPx = 16;

// Palette roles an ornament may use.
enum class OrnamentColor : uint8_t { kAccent, kPrimary, kSupporting, kDim, kDanger };

struct OrnamentLine {
  int16_t x1, y1, x2, y2;
  uint8_t width;
  OrnamentColor color;
};

struct OrnamentRect {
  int16_t x, y, w, h;  // top-left corner and size
  OrnamentColor color;
};

struct OrnamentShapes {
  static constexpr size_t kMaxLines = 48;
  static constexpr size_t kMaxRects = 32;

  std::array<OrnamentLine, kMaxLines> lines{};
  size_t line_count = 0;
  std::array<OrnamentRect, kMaxRects> rects{};
  size_t rect_count = 0;
};

// The shapes for an ornament; empty for kNone.
[[nodiscard]] OrnamentShapes BuildOrnament(Ornament ornament);

// True if every pixel of the shape (strokes included) lies inside the ornament zone.
[[nodiscard]] bool InOrnamentZone(const OrnamentLine& line);
[[nodiscard]] bool InOrnamentZone(const OrnamentRect& rect);

[[nodiscard]] Rgb OrnamentRgb(const ThemePalette& palette, OrnamentColor color);

}  // namespace sarathi::core
