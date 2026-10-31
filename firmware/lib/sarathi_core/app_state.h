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

// The phone's own maneuver icon (Chronos, ADR-0005): 48x48, 1 bit per pixel, row-major, most
// significant bit first.
constexpr size_t kManeuverIconSize = 48;
constexpr size_t kManeuverIconBytes = kManeuverIconSize * kManeuverIconSize / 8;

struct ManeuverIcon {
  bool valid = false;
  uint32_t id = 0;  // changes when the picture changes
  std::array<uint8_t, kManeuverIconBytes> bits{};
};

struct NavState {
  bool active = false;
  Maneuver next{};
  uint32_t distance_m = 0;  // to the next maneuver
  FixedString<48> street;   // the street after the next maneuver, or the phone's instruction
  uint32_t eta_s = 0;       // remaining time to the destination
  // Phone-formatted guidance (Chronos, ADR-0005), shown as given: the phone's language and
  // units. When set, they replace distance_m and eta_s, and `icon` replaces `next`.
  FixedString<16> distance_text;
  FixedString<16> remaining_text;
  ManeuverIcon icon{};
  bool has_window = false;
  RouteWindow window{};
  uint32_t window_received_ms = 0;
  // From our own phone link (protocol v1): the route is being recalculated, or the rider has
  // left it; and guidance that stopped arriving (protocol.md section 5). Never silent.
  bool rerouting = false;
  bool off_route = false;
  bool guidance_stale = false;
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

enum class Units : uint8_t { kMetric, kImperial };

// Backlight off after this long without input; never while navigating or in a call.
enum class ScreenTimeout : uint8_t { kNever, k30s, k1min, k5min };
constexpr size_t kScreenTimeoutCount = 4;

constexpr uint32_t ScreenTimeoutMs(ScreenTimeout t) {
  switch (t) {
    case ScreenTimeout::k30s:
      return 30U * 1000U;
    case ScreenTimeout::k1min:
      return 60U * 1000U;
    case ScreenTimeout::k5min:
      return 5U * 60U * 1000U;
    case ScreenTimeout::kNever:
      break;
  }
  return 0;
}

struct Settings {
  ThemeId theme = ThemeId::kClassic;
  int brightness_percent = 80;
  bool auto_day_night = false;  // dim the backlight at night (display_policy)
  Units units = Units::kMetric;
  ScreenTimeout screen_timeout = ScreenTimeout::kNever;
  bool knob_reversed = false;  // reverses rotation everywhere except the input test

  bool operator==(const Settings& o) const {
    return theme == o.theme && brightness_percent == o.brightness_percent &&
           auto_day_night == o.auto_day_night && units == o.units &&
           screen_timeout == o.screen_timeout && knob_reversed == o.knob_reversed;
  }
  bool operator!=(const Settings& o) const { return !(*this == o); }
};

// ---- Clock ----

// Local time of day from the phone (the demo ride's clock for now).
struct ClockState {
  bool valid = false;
  uint16_t minutes_of_day = 0;  // 0-1439
};

// ---- Ride ----

// At or above this speed the bike counts as moving, and settings are locked.
constexpr uint16_t kMovingThresholdKmh = 5;

struct RideState {
  uint16_t speed_kmh = 0;
  bool moving = false;
};

// ---- Phone ----

// What the data source can do (ADR-0005). The UI offers only what it supports.
struct Capabilities {
  bool call_accept = true;
  bool call_reject = true;  // false: the call screen can only be dismissed on the dash
  bool media_control = true;
  bool media_progress = true;  // track position and duration
  bool route_geometry = true;
  bool speed = true;           // false: settings lock while navigating instead (ADR-0005)
  bool phone_pairing = false;  // BLE pairing confirmed on the dash, and Forget phones
};

// The phone link, as the rider should understand it (our own link only; ADR-0007).
enum class LinkStatus : uint8_t {
  kOff,             // BLE paused for Wi-Fi
  kWaiting,         // advertising; no phone connected
  kSecuring,        // a phone is connected but the link isn't encrypted and paired yet
  kReady,           // secured, and the app said hello
  kUpdateApp,       // the app speaks an older protocol major version
  kUpdateFirmware,  // the app speaks a newer one
};

struct PhoneState {
  LinkStatus link = LinkStatus::kWaiting;
  bool connected = false;
  bool battery_valid = false;
  uint8_t battery_percent = 0;
  bool charging = false;
  uint8_t paired_phones = 0;  // bonds stored on the device
};

// The routing backend the phone uses, and the attribution its data needs (ADR-0006).
struct BackendState {
  bool valid = false;
  FixedString<24> name;
  FixedString<64> attribution;
  bool traffic = false;
  // The phone switched from another backend (a fallback, ADR-0006), and when.
  bool switched = false;
  uint32_t switched_ms = 0;
};

// A phone asks to pair: the rider compares the code with the phone's and confirms with the
// knob (CLAUDE.md, Security). Until then nothing from that phone is accepted.
constexpr uint32_t kPairingCodeMax = 999999;

struct PairingState {
  bool requested = false;
  uint32_t code = 0;  // 6 digits
  uint32_t since_ms = 0;
};

struct AppState {
  NavState nav;
  CallState call;
  MediaState media;
  Settings settings;
  RideState ride;
  ClockState clock;
  Capabilities caps;
  PhoneState phone;
  PairingState pairing;
  BackendState backend;
};

}  // namespace sarathi::core
