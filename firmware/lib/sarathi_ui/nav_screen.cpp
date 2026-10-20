#include "nav_screen.h"

#include <array>

#include "format.h"
#include "placeholder_style.h"
#include "static_label.h"

namespace sarathi::ui::nav_screen {
namespace {

// Route data older than this is shown as stale, never silently frozen (CLAUDE.md).
constexpr uint32_t kStaleAfterMs = 3000;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
StaticLabel<32> maneuver;
StaticLabel<24> distance;
StaticLabel<40> street;
StaticLabel<24> eta;
StaticLabel<32> route;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

lv_obj_t* Create() {
  lv_obj_t* screen = style::MakeScreen();
  maneuver.Create(screen, style::Essential(), style::kText, -115, style::kContentWidth);
  distance.Create(screen, style::Primary(), style::kText, -45, style::kContentWidth);
  street.Create(screen, style::Supporting(), style::kTextSupporting, 25, style::kContentWidth);
  eta.Create(screen, style::Supporting(), style::kTextSupporting, 85, style::kContentWidth);
  route.Create(screen, style::Supporting(), style::kTextSupporting, 145, 320);
  return screen;
}

void Update(const core::AppState& state, uint32_t now_ms) {
  const core::NavState& nav = state.nav;
  if (!nav.active) {
    maneuver.Set("%s", "No navigation");
    distance.Set("%s", "");
    street.Set("%s", "");
    eta.Set("%s", "");
    route.Set("%s", "");
    return;
  }

  std::array<char, 32> text{};
  core::FormatManeuver(nav.next, text.data(), text.size());
  maneuver.Set("%s", text.data());
  core::FormatDistance(nav.distance_m, text.data(), text.size());
  distance.Set("%s", nav.next.type == core::ManeuverType::kArrive && nav.distance_m == 0
                         ? "Arrived"
                         : text.data());
  street.Set("%s", nav.street.CStr());
  core::FormatDuration(nav.eta_s, text.data(), text.size());
  eta.Set("ETA %s", text.data());

  if (!nav.has_window) {
    route.Set("%s", "No route data");
  } else if (now_ms - nav.window_received_ms > kStaleAfterMs) {
    route.Set("Route stale %us", static_cast<unsigned>((now_ms - nav.window_received_ms) / 1000));
  } else {
    route.Set("Route %u pts", static_cast<unsigned>(nav.window.count));
  }
}

}  // namespace sarathi::ui::nav_screen
