#include "nav_screen.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "big_value.h"
#include "format.h"
#include "layout.h"
#include "maneuver_arrow.h"
#include "nav_status.h"
#include "nav_zoom.h"
#include "ring_gauge.h"
#include "route_view.h"
#include "static_label.h"
#include "theme_binding.h"
#include "visibility.h"

namespace sarathi::ui::nav_screen {
namespace {

// The ring fills over the last stretch before a maneuver.
constexpr float kRingRangeM = 500.0F;

// Layout (offsets from the screen centre). The map fills the whole circle; the rider sits in
// the lower third so most of the screen shows road ahead. The next maneuver (arrow and
// distance, the primary item) and the street sit on a translucent card at the top.
constexpr int32_t kRiderY = 370;  // screen y, lower third
constexpr int32_t kCardY = -118;
constexpr int32_t kCardWidth = 340;
constexpr int32_t kCardHeight = 150;
constexpr int32_t kArrowSize = 80;
constexpr int32_t kArrowX = -110;
constexpr int32_t kRowY = -140;  // arrow and distance share this row
constexpr int32_t kDistanceX = 40;
constexpr int32_t kDistanceWidth = 220;
constexpr int32_t kStreetY = -76;
// The status spot (core::NavStatus): one note at a time. The stopped-only attribution panel
// wraps to three lines there; at that height its corners stay inside the ring.
constexpr int32_t kRouteStatusY = 60;
constexpr int32_t kAttributionWidth = 380;
constexpr int32_t kAttributionLines = 3;
// Low enough to clear the rider chevron (+142), high enough that the label's bottom corners
// stay inside the ring gauge's inner edge (~225 px from the centre).
constexpr int32_t kEtaY = 172;
constexpr int32_t kRingDiameter = 470;

// Arrow mode: a source without route geometry (Chronos, ADR-0005). No map: the phone's icon,
// its distance text (the primary item), the instruction on up to two lines, and the remaining
// time. The icon is 48 px scaled 3x; at 4x its top would leave the circle.
constexpr int32_t kBigArrowSize = 144;
constexpr int32_t kBigArrowY = -112;
constexpr int32_t kBigDistanceY = 6;
constexpr int32_t kInstructionY = 92;
constexpr int32_t kInstructionWidth = 340;
constexpr int32_t kInstructionLines = 2;
constexpr int32_t kArrowEtaY = 165;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
RouteView route;
RingGauge ring;
lv_obj_t* card = nullptr;
ManeuverArrow arrow;
BigValue distance;
StaticLabel<49> street;        // NavState::street plus its terminator
StaticLabel<32> route_status;  // warnings
StaticLabel<40> notice;        // "Now: <backend>"
StaticLabel<96> attribution;   // stopped only
StaticLabel<24> eta;
ManeuverArrow big_arrow;
BigValue big_distance;
StaticLabel<64> instruction;
StaticLabel<24> arrow_eta;
bool arrow_mode = false;
#ifdef SARATHI_NAV_STRESS
uint16_t stress_spin = 0;  // extra heading, degrees
#endif
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

core::ProjectionParams MapProjection() {
  core::ProjectionParams p;
  p.metres_per_px = core::kZoomFarMetresPerPx;
  p.rider = {layout::kCenter, kRiderY};
  p.circle_center = {layout::kCenter, layout::kCenter};
  p.circle_radius_px = 225.0F;  // inside the ring gauge
  return p;
}

// A label that only shows (with its card) when it has text. Touches the flag only when it
// changes: lv_obj_add/remove_flag(HIDDEN) invalidates the object even if nothing changed,
// which would force a redraw on every update.
template <size_t N>
void SetBubble(StaticLabel<N>& label, const char* text) {
  label.Set("%s", text);
  SetVisible(label.Obj(), text[0] != '\0');
}

// Shows the note in its own label (each has its role's style); hides the others.
void ShowNote(const core::NavNote& note) {
  using Kind = core::NavNoteKind;
  SetBubble(route_status, note.kind == Kind::kWarning ? note.text.CStr() : "");
  SetBubble(notice, note.kind == Kind::kNotice ? note.text.CStr() : "");
  SetBubble(attribution, note.kind == Kind::kAttribution ? note.text.CStr() : "");
}

// One layout or the other; the hidden one keeps its state for when it comes back.
void ShowArrowMode(bool on) {
  arrow_mode = on;
  SetVisible(card, !on);
  ring.SetVisible(!on);
  arrow.SetVisible(!on);
  distance.SetVisible(!on);
  SetVisible(street.Obj(), !on);
  big_arrow.SetVisible(on);
  big_distance.SetVisible(on);
  SetVisible(instruction.Obj(), on);
  route.SetVisible(!on);
  if (on) {
    route.Clear();
    ShowNote(core::NavNote{});
    SetBubble(eta, "");
  } else {
    SetBubble(arrow_eta, "");
  }
}

void UpdateArrowMode(const core::AppState& state) {
  const core::NavState& nav = state.nav;
  if (!nav.active) {
    big_arrow.SetIcon(core::ManeuverIcon{});
    big_arrow.Set(core::Maneuver{});
    big_distance.SetValue("");
    instruction.Set("%s", core::NavIdleText(state));
    SetBubble(arrow_eta, "");
    return;
  }
  big_arrow.Set(nav.next);
  big_arrow.SetIcon(nav.icon);
  std::array<char, 32> text{};
  if (!nav.distance_text.Empty()) {
    big_distance.SetValue(nav.distance_text.CStr());
  } else {
    core::FormatDistance(nav.distance_m, state.settings.units, text.data(), text.size());
    big_distance.SetValue(text.data());
  }
  instruction.Set("%s", nav.street.CStr());
  std::array<char, 24> eta_text{};
  if (!nav.remaining_text.Empty()) {
    std::snprintf(eta_text.data(), eta_text.size(), "ETA %s", nav.remaining_text.CStr());
  }
  SetBubble(arrow_eta, eta_text.data());
}

}  // namespace

lv_obj_t* Create() {
  lv_obj_t* screen = theme::MakeScreen();
  // Draw order: map, card, ring, then text. The ring sits above the card so the card never
  // dims it.
  route.Create(screen, 0, MapProjection());

  card = lv_obj_create(screen);
  lv_obj_remove_style_all(card);
  lv_obj_add_style(card, theme::CardStyle(), 0);
  lv_obj_set_size(card, kCardWidth, kCardHeight);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, kCardY);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  ring.Create(screen, kRingDiameter);
  arrow.Create(screen, kArrowSize, kArrowX, kRowY);
  distance.Create(screen, kRowY, kDistanceWidth, kDistanceX);
  street.Create(screen, theme::Text::kSupporting, kStreetY, kCardWidth - 20);

  route_status.Create(screen, theme::Text::kWarning, kRouteStatusY);
  lv_obj_add_style(route_status.Obj(), theme::CardStyle(), 0);
  notice.Create(screen, theme::Text::kSupporting, kRouteStatusY);
  lv_obj_add_style(notice.Obj(), theme::CardStyle(), 0);
  attribution.Create(screen, theme::Text::kStoppedOnly, kRouteStatusY, kAttributionWidth);
  lv_obj_add_style(attribution.Obj(), theme::CardStyle(), 0);
  const lv_font_t* small = lv_obj_get_style_text_font(attribution.Obj(), LV_PART_MAIN);
  lv_obj_set_height(attribution.Obj(), kAttributionLines * lv_font_get_line_height(small));
  eta.Create(screen, theme::Text::kSupporting, kEtaY);
  lv_obj_add_style(eta.Obj(), theme::CardStyle(), 0);

  big_arrow.Create(screen, kBigArrowSize, 0, kBigArrowY);
  big_distance.Create(screen, kBigDistanceY);
  instruction.Create(screen, theme::Text::kSupporting, kInstructionY, kInstructionWidth);
  const lv_font_t* font = lv_obj_get_style_text_font(instruction.Obj(), LV_PART_MAIN);
  lv_obj_set_height(instruction.Obj(), kInstructionLines * lv_font_get_line_height(font));
  arrow_eta.Create(screen, theme::Text::kSupporting, kArrowEtaY);
  lv_obj_add_style(arrow_eta.Obj(), theme::CardStyle(), 0);
  ShowArrowMode(false);
  return screen;
}

void Update(const core::AppState& state, uint32_t now_ms) {
  if (arrow_mode == state.caps.route_geometry) {
    ShowArrowMode(!state.caps.route_geometry);
  }
  if (arrow_mode) {
    UpdateArrowMode(state);
    return;
  }
  const core::NavState& nav = state.nav;
  if (!nav.active) {
    arrow.Set(core::Maneuver{});
    distance.SetValue("");
    street.Set("%s", core::NavIdleText(state));
    ring.Set(0.0F);
    route.Clear();
    ShowNote(core::NavNote{});
    SetBubble(eta, "");
    return;
  }

  arrow.Set(nav.next);
  std::array<char, 32> text{};
  if (nav.next.type == core::ManeuverType::kArrive && nav.distance_m == 0) {
    distance.SetValue("Arrived");
  } else {
    core::FormatDistance(nav.distance_m, state.settings.units, text.data(), text.size());
    distance.SetValue(text.data());
  }
  street.Set("%s", nav.street.CStr());
  ring.Set(1.0F - std::min(1.0F, static_cast<float>(nav.distance_m) / kRingRangeM));
  core::FormatDuration(nav.eta_s, text.data(), text.size());
  std::array<char, 24> eta_text{};
  std::snprintf(eta_text.data(), eta_text.size(), "ETA %s", text.data());
  SetBubble(eta, eta_text.data());

  ShowNote(core::NavStatus(state, now_ms));
  if (!nav.has_window) {
    route.Clear();
    return;
  }
  const bool stale = now_ms - nav.window_received_ms > core::kPositionStaleMs;
  const float zoom = core::NavMetresPerPx(nav.distance_m);
#ifdef SARATHI_NAV_STRESS
  // Worst case for smooth rotation: re-project and redraw the map on every update.
  core::RouteWindow spun = nav.window;
  spun.heading_deg = static_cast<uint16_t>((spun.heading_deg + stress_spin) % 360);
  stress_spin = static_cast<uint16_t>((stress_spin + 2) % 360);
  route.Set(spun, stale, zoom, true);
#else
  route.Set(nav.window, stale, zoom);
#endif
}

}  // namespace sarathi::ui::nav_screen
