// App state shown by the UI: navigation (with the route window), call, media, settings and
// ride. Internal types only; the phone protocol (docs/protocol/, Phase 4) maps onto them.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "fixed_string.h"

namespace sarathi::core {

// ---- Navigation ----

enum class ManeuverType : uint8_t {
  kNone,
  kDepart,
  kStraight,
  kSlightLeft,
  kLeft,
  kSharpLeft,
  kSlightRight,
  kRight,
  kSharpRight,
  kUTurn,
  kRoundabout,
  kArrive,
};

struct Maneuver {
  ManeuverType type = ManeuverType::kNone;
  uint8_t roundabout_exit = 0;  // 1-based; only for kRoundabout

  bool operator==(const Maneuver& o) const {
    return type == o.type && roundabout_exit == o.roundabout_exit;
  }
  bool operator!=(const Maneuver& o) const { return !(*this == o); }
};

// A point in metres relative to the rider: x east, y north (ADR-0002).
struct RoutePoint {
  int16_t x_m = 0;
  int16_t y_m = 0;
};

constexpr size_t kMaxRoutePoints = 64;

// The part of the route around the rider, simplified for the screen (ADR-0002).
struct RouteWindow {
  std::array<RoutePoint, kMaxRoutePoints> points{};
  uint8_t count = 0;
  uint16_t heading_deg = 0;  // direction of travel, clockwise from north, 0-359
  bool has_maneuver_point = false;
  RoutePoint maneuver_point{};  // where the next maneuver is, relative to the rider
  uint32_t sequence = 0;        // increases with every new window
};

struct NavState {
  bool active = false;
  Maneuver next{};
  uint32_t distance_m = 0;  // to the next maneuver
  FixedString<32> street;   // the street after the next maneuver
  uint32_t eta_s = 0;       // remaining time to the destination
  bool has_window = false;
  RouteWindow window{};
  uint32_t window_received_ms = 0;
};

// ---- Call ----

enum class CallPhase : uint8_t { kIdle, kRinging, kActive };

struct CallState {
  CallPhase phase = CallPhase::kIdle;
  FixedString<32> caller;
  uint32_t phase_since_ms = 0;  // when the current phase started
};

// ---- Media ----

struct MediaState {
  bool available = false;
  bool playing = false;
  FixedString<48> title;
  FixedString<48> artist;
  uint32_t position_s = 0;
  uint32_t duration_s = 0;
};

// ---- Settings ----

enum class ThemeId : uint8_t { kClassic, kFighterHud, kRacing };
constexpr size_t kThemeCount = 3;

struct Settings {
  ThemeId theme = ThemeId::kClassic;
  int brightness_percent = 80;
};

// ---- Ride ----

// At or above this speed the bike counts as moving, and settings are locked.
constexpr uint16_t kMovingThresholdKmh = 5;

struct RideState {
  uint16_t speed_kmh = 0;
  bool moving = false;
};

struct AppState {
  NavState nav;
  CallState call;
  MediaState media;
  Settings settings;
  RideState ride;
};

}  // namespace sarathi::core
