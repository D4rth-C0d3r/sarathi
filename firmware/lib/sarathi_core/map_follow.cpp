#include "map_follow.h"

#include <algorithm>
#include <cmath>

namespace sarathi::core {

namespace {

constexpr float kDegToRad = 3.14159265F / 180.0F;
constexpr float kMsPerS = 1000.0F;
constexpr float kKmhPerMps = 3.6F;
constexpr float kFullTurn = 360.0F;
constexpr float kHalfTurn = 180.0F;
constexpr float kZoomEasePerS = 0.8F;  // fraction of the gap closed per second
constexpr float kHeadingRateDegPerS = 120.0F;
constexpr float kRedrawHeadingDeg = 1.5F;
constexpr float kRedrawPx = 1.0F;
constexpr float kRedrawZoomRatio = 0.02F;  // 2 %

// The signed shortest turn from `from` to `to`, -180..180.
float Turn(float from, float to) {
  float d = std::fmod(to - from, kFullTurn);
  if (d > kHalfTurn) d -= kFullTurn;
  if (d < -kHalfTurn) d += kFullTurn;
  return d;
}

float Wrap(float deg) {
  float h = std::fmod(deg, kFullTurn);
  return h < 0 ? h + kFullTurn : h;
}

}  // namespace

float ZoomForSpeed(float kmh) {
  const float f = std::clamp((kmh - kZoomSlowKmh) / (kZoomFastKmh - kZoomSlowKmh), 0.0F, 1.0F);
  return kZoomSlowMetresPerPx + f * (kZoomFastMetresPerPx - kZoomSlowMetresPerPx);
}

MapViewPoint MapFollow::Update(const RiderPose& pose, bool has_maneuver, uint32_t to_maneuver_m,
                               uint32_t now_ms) {
  const float kmh = pose.speed_mps * kKmhPerMps;
  if (!started_) {
    started_ = true;
    last_ms_ = now_ms;
    target_kmh_ = kmh;
    heading_ = pose.heading_deg;
    zoom_ = ZoomForSpeed(kmh);
  }
  const float dt = static_cast<float>(now_ms - last_ms_) / kMsPerS;
  last_ms_ = now_ms;
  if (std::fabs(kmh - target_kmh_) >= kZoomHysteresisKmh) {
    target_kmh_ = kmh;
  }
  const bool near_turn = has_maneuver && to_maneuver_m <= kZoomTurnM;
  const float target = near_turn ? kZoomSlowMetresPerPx : ZoomForSpeed(target_kmh_);
  zoom_ += (target - zoom_) * std::min(1.0F, kZoomEasePerS * dt);
  const float turn = Turn(heading_, pose.heading_deg);
  const float step = kHeadingRateDegPerS * dt;
  heading_ = Wrap(heading_ + std::clamp(turn, -step, step));

  // Dead reckoning from the pose, along its heading, for at most kDeadReckonMaxMs.
  const uint32_t since = std::min(now_ms - pose.received_ms, kDeadReckonMaxMs);
  const float travelled = pose.speed_mps * static_cast<float>(since) / kMsPerS;
  const float h = pose.heading_deg * kDegToRad;
  return MapViewPoint{pose.x_m + std::sin(h) * travelled, pose.y_m + std::cos(h) * travelled,
                      heading_, zoom_};
}

bool MapFollow::NeedsRedraw(const MapViewPoint& drawn, const MapViewPoint& now) {
  const float moved_px = std::hypot(now.x_m - drawn.x_m, now.y_m - drawn.y_m) / now.metres_per_px;
  const float zoom_change =
      std::fabs(now.metres_per_px - drawn.metres_per_px) / drawn.metres_per_px;
  return std::fabs(Turn(drawn.heading_deg, now.heading_deg)) >= kRedrawHeadingDeg ||
         moved_px >= kRedrawPx || zoom_change >= kRedrawZoomRatio;
}

}  // namespace sarathi::core
