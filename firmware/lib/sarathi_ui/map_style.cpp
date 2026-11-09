#include "map_style.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "theme_binding.h"

namespace sarathi::ui {

namespace {

// Motorway to service: brighter for bigger roads, all quieter than the route.
constexpr std::array<core::Rgb, core::kRoadClasses> kRoad = {0x8A8F99, 0x8A8F99, 0x7A7F88, 0x6A6F78,
                                                             0x5A5F68, 0x464B52, 0x363A40};
// Widths at 2 m/px; thinner when zoomed out.
constexpr std::array<float, core::kRoadClasses> kRoadPx = {7, 7, 6, 5, 4, 3, 2};
constexpr core::Rgb kWater = 0x1E3A5F;
constexpr core::Rgb kPark = 0x1E3F2A;
constexpr float kBaseMetresPerPx = 2.0F;

}  // namespace

core::MapStyle MapStyleFor(const core::ThemeDescriptor& theme, float metres_per_px) {
  core::MapStyle s;
  s.background = theme::Pixel(theme.palette.background);
  s.water = theme::Pixel(kWater);
  s.park = theme::Pixel(kPark);
  const float scale = kBaseMetresPerPx / std::max(metres_per_px, kBaseMetresPerPx);
  for (size_t i = 0; i < kRoad.size(); ++i) {
    s.road[i] = theme::Pixel(kRoad.at(i));  // NOLINT
    s.road_px[i] =
        static_cast<uint8_t>(std::max(1.0F, std::round(kRoadPx.at(i) * scale)));  // NOLINT
  }
  return s;
}

}  // namespace sarathi::ui
