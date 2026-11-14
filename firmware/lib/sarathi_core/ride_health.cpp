#include "ride_health.h"

#include <algorithm>
#include <cmath>

#include "nav_status.h"

namespace sarathi::core {

void RideHealth::OnChipTemp(bool valid, float celsius) {
  if (!valid || !std::isfinite(celsius)) {
    temp_valid_ = false;
    return;
  }
  temp_c_ = static_cast<int>(std::lround(celsius));
  max_temp_c_ = any_reading_ ? std::max(max_temp_c_, temp_c_) : temp_c_;
  any_reading_ = true;
  temp_valid_ = true;
  if (!hot_ && temp_c_ >= kHotChipC) {
    hot_ = true;
    ++hot_count_;
  } else if (hot_ && temp_c_ < kCoolChipC) {
    hot_ = false;
  }
}

void RideHealth::Update(const AppState& state, uint32_t now_ms) {
  const bool connected = state.phone.connected;
  if (was_connected_ && !connected && state.phone.link != LinkStatus::kOff) {
    ++phone_lost_;
  }
  was_connected_ = connected;

  const bool stale = RouteDataStale(state, now_ms);
  if (stale && !was_stale_) {
    ++stale_count_;
  }
  was_stale_ = stale;
}

int RideHealth::CapBacklight(int percent) const {
  return hot_ ? std::min(percent, kHotBacklightPercent) : percent;
}

bool RouteDataStale(const AppState& state, uint32_t now_ms) {
  const NavState& nav = state.nav;
  if (!nav.active) {
    return false;
  }
  if (nav.guidance_stale) {
    return true;
  }
  return state.caps.route_geometry && nav.has_window &&
         now_ms - nav.window_received_ms > kPositionStaleMs;
}

}  // namespace sarathi::core
