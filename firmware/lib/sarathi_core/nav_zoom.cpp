#include "nav_zoom.h"

namespace sarathi::core {

float NavMetresPerPx(uint32_t distance_to_maneuver_m) {
  if (distance_to_maneuver_m <= kZoomNearM) {
    return kZoomNearMetresPerPx;
  }
  if (distance_to_maneuver_m >= kZoomFarM) {
    return kZoomFarMetresPerPx;
  }
  const float t = static_cast<float>(distance_to_maneuver_m - kZoomNearM) /
                  static_cast<float>(kZoomFarM - kZoomNearM);
  return kZoomNearMetresPerPx + (t * (kZoomFarMetresPerPx - kZoomNearMetresPerPx));
}

}  // namespace sarathi::core
