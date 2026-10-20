// Vector arrow shapes for maneuvers, in a 100 x 100 box (x right, y down). sarathi_ui scales
// them and draws them in the theme's stroke width and colour. Pure data.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app_state.h"

namespace sarathi::core {

constexpr int kGlyphSize = 100;

struct GlyphPoint {
  int8_t x = 0;
  int8_t y = 0;
};

struct GlyphStroke {
  std::array<GlyphPoint, 8> points{};
  uint8_t count = 0;
};

struct ManeuverGlyph {
  std::array<GlyphStroke, 3> strokes{};
  uint8_t stroke_count = 0;
  bool has_ring = false;  // roundabout
  GlyphPoint ring_center{};
  uint8_t ring_radius = 0;
};

[[nodiscard]] ManeuverGlyph GlyphFor(const Maneuver& maneuver);

}  // namespace sarathi::core
