// Auto-zoom for the Nav map (ADR-0002): zoomed out when the next turn is far, so more road
// ahead shows; zoomed in near it, so the junction is large. Pure logic.

#pragma once

#include <cstdint>

namespace sarathi::core {

constexpr float kZoomNearMetresPerPx = 2.0F;  // at or closer than kZoomNearM
constexpr float kZoomFarMetresPerPx = 6.0F;   // at or further than kZoomFarM
constexpr uint32_t kZoomNearM = 150;
constexpr uint32_t kZoomFarM = 1000;

// Map scale for a distance to the next maneuver; linear between near and far.
[[nodiscard]] float NavMetresPerPx(uint32_t distance_to_maneuver_m);

}  // namespace sarathi::core
