#include "nav_screen.h"

#include <algorithm>
#include <array>

#include "big_value.h"
#include "format.h"
#include "layout.h"
#include "maneuver_arrow.h"
#include "ring_gauge.h"
#include "route_view.h"
#include "static_label.h"
#include "theme_binding.h"

namespace sarathi::ui::nav_screen {
namespace {

// Route data older than this is shown as stale, never silently frozen (CLAUDE.md).
constexpr uint32_t kStaleAfterMs = 3000;
// The ring fills over the last stretch before a maneuver.
constexpr float kRingRangeM = 500.0F;

// Layout (screen centre = 0). Top half: maneuver and distance (primary). Lower half: map.
constexpr int32_t kArrowSize = 96;
constexpr int32_t kArrowY = -148;
constexpr int32_t kDistanceY = -72;  // street caption sits below it
constexpr int32_t kMapTop = 230;     // screen y where the map area starts
constexpr int32_t kRouteStatusY = 70;
// Low enough to clear the rider chevron (+142), high enough that the label's bottom corners
// stay inside the ring gauge's inner edge (~225 px from the centre).
constexpr int32_t kEtaY = 172;
constexpr int32_t kRingDiameter = 470;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
RouteView route;
RingGauge ring;
ManeuverArrow arrow;
BigValue distance;
StaticLabel<32> route_status;
StaticLabel<24> eta;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

core::ProjectionParams MapProjection() {
  core::ProjectionParams p;
  p.metres_per_px = 3.5F;  // about 500 m ahead visible above the rider
  p.rider = {layout::kCenter, 370};
  p.circle_center = {layout::kCenter, layout::kCenter};
  p.circle_radius_px = 225.0F;  // inside the ring gauge
  return p;
}

}  // namespace

lv_obj_t* Create() {
  lv_obj_t* screen = theme::MakeScreen();
  route.Create(screen, kMapTop, MapProjection());  // first, so text draws on top
  ring.Create(screen, kRingDiameter);
  arrow.Create(screen, kArrowSize, 0, kArrowY);
  distance.Create(screen, kDistanceY);
  route_status.Create(screen, theme::Text::kWarning, kRouteStatusY, 300);
  eta.Create(screen, theme::Text::kSupporting, kEtaY, 220);
  return screen;
}

void Update(const core::AppState& state, uint32_t now_ms) {
  const core::NavState& nav = state.nav;
  if (!nav.active) {
    arrow.Set(core::Maneuver{});
    distance.SetValue("");
    distance.SetCaption("No navigation");
    ring.Set(0.0F);
    route.Clear();
    route_status.Set("%s", "");
    eta.Set("%s", "");
    return;
  }

  arrow.Set(nav.next);
  std::array<char, 32> text{};
  if (nav.next.type == core::ManeuverType::kArrive && nav.distance_m == 0) {
    distance.SetValue("Arrived");
  } else {
    core::FormatDistance(nav.distance_m, text.data(), text.size());
    distance.SetValue(text.data());
  }
  distance.SetCaption(nav.street.CStr());
  ring.Set(1.0F - std::min(1.0F, static_cast<float>(nav.distance_m) / kRingRangeM));
  core::FormatDuration(nav.eta_s, text.data(), text.size());
  eta.Set("ETA %s", text.data());

  const bool stale = nav.has_window && now_ms - nav.window_received_ms > kStaleAfterMs;
  if (!nav.has_window) {
    route.Clear();
    route_status.Set("%s", "No route data");
  } else {
    route.Set(nav.window, stale);
    if (stale) {
      route_status.Set("Route stale %us",
                       static_cast<unsigned>((now_ms - nav.window_received_ms) / 1000));
    } else {
      route_status.Set("%s", "");
    }
  }
}

}  // namespace sarathi::ui::nav_screen
