#include "map_style.h"

#include <algorithm>
#include <cmath>

#include "theme_binding.h"

namespace sarathi::ui {

namespace {

constexpr float kBaseMetresPerPx = 2.0F;

uint8_t Scaled(uint8_t px, float scale) {
  return px == 0 ? 0 : static_cast<uint8_t>(std::max(1.0F, std::round(px * scale)));
}

}  // namespace

core::MapStyle MapStyleFor(const core::ThemeDescriptor& theme, float metres_per_px) {
  const core::ThemeMapStyle& m = theme.map;
  core::MapStyle s;
  s.background = theme::Pixel(m.ground);
  s.draw_polygons = m.fill_areas;
  s.water = theme::Pixel(m.water);
  s.park = theme::Pixel(m.park);
  // Widths are for 2 m/px; thinner when zoomed out.
  const float scale = kBaseMetresPerPx / std::max(metres_per_px, kBaseMetresPerPx);
  for (size_t i = 0; i < core::kMapRoadClasses; ++i) {
    s.road[i] = theme::Pixel(m.road.at(i));         // NOLINT(*-constant-array-index)
    s.road_px[i] = Scaled(m.road_px.at(i), scale);  // NOLINT(*-constant-array-index)
    s.glow[i] = theme::Pixel(m.glow.at(i));         // NOLINT(*-constant-array-index)
  }
  s.glow_extra_px = Scaled(m.glow_extra_px, scale);
  return s;
}

}  // namespace sarathi::ui
