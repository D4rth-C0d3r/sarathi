// How the map follows the rider (ADR-0009 "Device: drawing"), pure logic:
// - Zoom by speed: 2 m/px up to 20 km/h, rising linearly to 10 m/px at 100 km/h; within 300 m of
//   a maneuver it eases to 2 m/px. Speed changes under 5 km/h don't move the target
//   (hysteresis), and the zoom eases towards it, so it doesn't pump.
// - Between poses (2 Hz) the position is dead-reckoned along the heading at the pose's speed, for
//   at most 5 s; the heading turns towards the pose's at a limited rate.
// - The map redraws only when the view moved enough to see: 1.5 degrees, 1 px, or the zoom.

#pragma once

#include <cstdint>

namespace sarathi::core {

// The phone's latest pose in the ride frame (protocol `pose`).
struct RiderPose {
  bool valid = false;
  float x_m = 0;
  float y_m = 0;
  float heading_deg = 0;  // clockwise from north
  float speed_mps = 0;
  uint32_t received_ms = 0;
};

struct MapViewPoint {
  float x_m = 0;
  float y_m = 0;
  float heading_deg = 0;
  float metres_per_px = 2;
};

constexpr float kZoomSlowMetresPerPx = 2.0F;
constexpr float kZoomFastMetresPerPx = 10.0F;
constexpr float kZoomSlowKmh = 20.0F;
constexpr float kZoomFastKmh = 100.0F;
constexpr uint32_t kZoomTurnM = 300;
constexpr float kZoomHysteresisKmh = 5.0F;
constexpr uint32_t kDeadReckonMaxMs = 5000;

// The zoom for a speed, before easing.
[[nodiscard]] float ZoomForSpeed(float kmh);

class MapFollow {
 public:
  // The view at `now_ms`. `to_maneuver_m` 0 with `has_maneuver` false means none.
  MapViewPoint Update(const RiderPose& pose, bool has_maneuver, uint32_t to_maneuver_m,
                      uint32_t now_ms);

  // Whether `now` differs enough from `drawn` to be seen.
  [[nodiscard]] static bool NeedsRedraw(const MapViewPoint& drawn, const MapViewPoint& now);

  void Reset() { started_ = false; }

 private:
  bool started_ = false;
  uint32_t last_ms_ = 0;
  float target_kmh_ = 0;
  float zoom_ = kZoomSlowMetresPerPx;
  float heading_ = 0;
};

}  // namespace sarathi::core
