#include "nav_screen.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "big_value.h"
#include "format.h"
#include "layout.h"
#include "maneuver_arrow.h"
#include "map_follow.h"
#include "map_style.h"
#include "map_theme.h"
#include "map_view.h"
#include "nav_status.h"
#include "nav_zoom.h"
#include "ring_gauge.h"
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
MapView map;
core::ChunkStore* map_store = nullptr;      // SetMapData(); null: no map source
core::MapRenderer* map_renderer = nullptr;  // SetMapData()
uint32_t (*map_time_us)() = nullptr;        // SetMapData()
uint32_t map_sequence = 0;                  // the window last drawn
bool map_stale = false;
const core::ThemeDescriptor* map_theme = nullptr;
core::MapFollow follow;          // zoom and motion between poses (ADR-0009)
core::MapViewPoint map_drawn{};  // the view last drawn
uint32_t map_drawn_ms = 0;
uint32_t map_traffic_ms = 0;  // the traffic last drawn, and whether it was there or old
bool map_traffic_valid = false;
bool map_traffic_old = false;
StaticLabel<24> map_note;         // "Map loading", or the map's attribution when it starts
uint32_t map_empty_since_ms = 0;  // no chunk in view since then (0: chunks are in view)
uint32_t map_started_ms = 0;      // the first chunks of this ride were drawn then
bool map_started = false;
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

// A label that only shows (with its card) when it has text. Touches the flag only when it
// changes: lv_obj_add/remove_flag(HIDDEN) invalidates the object even if nothing changed,
// which would force a redraw on every update.
template <size_t N>
void SetBubble(StaticLabel<N>& label, const char* text) {
  label.Set("%s", text);
  SetVisible(label.Obj(), text[0] != '\0');
}

constexpr float kMapCircleRadius = 225.0F;   // inside the ring gauge
constexpr float kL1AboveMetresPerPx = 4.0F;  // ADR-0009: L1 only above 4 m/px

void ClearMap() {
  map.SetVisible(false);
  map_sequence = 0;
  map_started = false;
  map_empty_since_ms = 0;
  SetBubble(map_note, "");
}

constexpr uint32_t kMapFrameMs = 66;           // at most 15 fps (ADR-0009)
constexpr uint32_t kMapLoadingAfterMs = 2000;  // don't flash it at every start
constexpr uint32_t kMapAttributionMs = 10000;  // ADR-0009: shown when the map starts
constexpr int32_t kMapNoteY = 116;             // between the status spot and the ETA

// Never silent about the map (ADR-0009): "Map loading" while the phone's map isn't here yet, and
// OpenStreetMap's attribution for a while once it is. Only with a map source.
void UpdateMapNote(uint32_t now_ms) {
  const bool loaded = map.ChunksDrawn() > 0;
  if (map_store == nullptr) {
    SetBubble(map_note, "");
    return;
  }
  if (loaded && !map_started) {
    map_started = true;
    map_started_ms = now_ms;
  }
  map_empty_since_ms = loaded ? 0 : (map_empty_since_ms == 0 ? now_ms : map_empty_since_ms);
  if (!loaded && now_ms - map_empty_since_ms >= kMapLoadingAfterMs) {
    SetBubble(map_note, "Map loading");
  } else if (map_started && now_ms - map_started_ms < kMapAttributionMs) {
    SetBubble(map_note, "(c) OpenStreetMap");  // the built-in fonts have no ©; ODbL (ADR-0009)
  } else {
    SetBubble(map_note, "");
  }
}

// The route by traffic level, from the theme (core::RouteLookFor, checked by the legibility
// tests): never by colour alone.
using RouteStyles4 = core::MapRouteStyle[core::kTrafficLevels];  // NOLINT(*-avoid-c-arrays)
const RouteStyles4& RouteStyles(const core::ThemeDescriptor& t, bool stale) {
  static RouteStyles4 styles;  // NOLINT(*-avoid-non-const-global-variables)
  for (size_t i = 0; i < core::kTrafficLevels; ++i) {
    const core::RouteLook look = core::RouteLookFor(t, static_cast<core::TrafficLevel>(i), stale);
    styles[i] = {theme::Pixel(look.color), look.width_px, look.pattern};  // NOLINT
  }
  return styles;
}

// The map's camera: heading up, the rider in the lower third.
core::MapCamera CameraAt(const core::MapViewPoint& view) {
  core::MapCamera camera;
  camera.x_m = view.x_m;
  camera.y_m = view.y_m;
  camera.heading_deg = view.heading_deg;
  camera.metres_per_px = view.metres_per_px;
  camera.anchor_x = layout::kCenter;
  camera.anchor_y = kRiderY;
  camera.view_cx = layout::kCenter;
  camera.view_cy = layout::kCenter;
  camera.view_r = static_cast<int32_t>(kMapCircleRadius);
  return camera;
}

// Where the map looks from now. With poses (our phone link): dead-reckoned between them, zoom by
// speed and before turns. Without (the demo ride, Chronos): the window's own position and heading,
// zoom by distance to the turn, as before.
core::MapViewPoint ViewNow(const core::AppState& state, uint32_t now_ms, bool* moving) {
  const core::NavState& nav = state.nav;
  const core::RouteWindow& w = nav.window;
  *moving = state.rider.valid && w.has_position;
  if (*moving) {
    const bool has_turn = nav.next.type != core::ManeuverType::kNone;
    return follow.Update(state.rider, has_turn, nav.distance_m, now_ms);
  }
  follow.Reset();
  return core::MapViewPoint{w.has_position ? w.rider_x_m : 0.0F,
                            w.has_position ? w.rider_y_m : 0.0F, static_cast<float>(w.heading_deg),
                            core::NavMetresPerPx(nav.distance_m)};
}

// The map with the route window over it. Redraws for a new window, staleness, theme, or (with
// poses) a view that moved enough to see, at most 15 times a second; a still bike costs nothing.
void ShowMap(const core::AppState& state, bool stale, uint32_t now_ms, bool force) {
  const core::RouteWindow& window = state.nav.window;
  const core::ThemeDescriptor& t = theme::Current();
  bool moving = false;
  const core::MapViewPoint view = ViewNow(state, now_ms, &moving);
  const core::TrafficState& traffic = state.traffic;
  const bool traffic_old = traffic.valid && now_ms - traffic.received_ms > core::kTrafficStaleMs;
  const bool data_changed = window.sequence != map_sequence || stale != map_stale ||
                            &t != map_theme || traffic.received_ms != map_traffic_ms ||
                            traffic.valid != map_traffic_valid || traffic_old != map_traffic_old;
  const bool view_changed = moving && core::MapFollow::NeedsRedraw(map_drawn, view) &&
                            now_ms - map_drawn_ms >= kMapFrameMs;
  if (!force && !data_changed && !view_changed) {
    return;
  }
  map_sequence = window.sequence;
  map_stale = stale;
  map_theme = &t;
  map_traffic_ms = traffic.received_ms;
  map_traffic_valid = traffic.valid;
  map_traffic_old = traffic_old;
  map_drawn = view;
  map_drawn_ms = now_ms;
  if (data_changed || force) {
    map.SetRoute(window, state.traffic, now_ms, RouteStyles(t, stale), t.palette.maneuver,
                 stale ? LV_OPA_40 : LV_OPA_COVER);
    map.SetRider(t.palette.rider);
  }
  const float mpp = view.metres_per_px;
  map.Set(CameraAt(view), mpp > kL1AboveMetresPerPx ? 1 : 0, MapStyleFor(t, mpp));
  map.SetVisible(true);
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
  if (on) {
    ClearMap();
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

MapPerf TakeMapPerf() {
  const MapView::Stats s = map.TakeStats();
  return MapPerf{s.draws, s.draw_avg_us * s.draws, s.last.prepare_us, s.last.lines_us};
}

void SetMapData(core::ChunkStore* store, core::MapRenderer* renderer, uint32_t (*time_us)()) {
  map_store = store;
  map_renderer = renderer;
  map_time_us = time_us;
}

lv_obj_t* Create() {
  lv_obj_t* screen = theme::MakeScreen();
  // Draw order: map, card, ring, then text. The ring sits above the card so the card never
  // dims it.
  map.Create(screen, 0, 0, layout::kScreenSize, layout::kScreenSize, map_store, map_renderer,
             map_time_us);
  map.SetVisible(false);

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
  map_note.Create(screen, theme::Text::kSupporting, kMapNoteY);
  lv_obj_add_style(map_note.Obj(), theme::CardStyle(), 0);
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
    ClearMap();
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
    ClearMap();
    return;
  }
  const bool stale = now_ms - nav.window_received_ms > core::kPositionStaleMs;
#ifdef SARATHI_NAV_STRESS
  // Worst case for smooth rotation: re-project and redraw the map on every update.
  core::AppState spun = state;
  spun.nav.window.heading_deg =
      static_cast<uint16_t>((spun.nav.window.heading_deg + stress_spin) % 360);
  stress_spin = static_cast<uint16_t>((stress_spin + 2) % 360);
  ShowMap(spun, stale, now_ms, true);
#else
  ShowMap(state, stale, now_ms, false);
#endif
  UpdateMapNote(now_ms);
}

}  // namespace sarathi::ui::nav_screen
