#include "nav_status.h"

#include <array>
#include <cstdio>

namespace sarathi::core {
namespace {

constexpr uint32_t kMsPerSecond = 1000;

NavNote Note(NavNoteKind kind, FontRole role, const char* text) {
  NavNote note;
  note.kind = kind;
  note.role = role;
  note.text.Assign(text);
  return note;
}

}  // namespace

NavNote NavStatus(const AppState& state, uint32_t now_ms) {
  const NavState& nav = state.nav;
  if (!nav.active) {
    return NavNote{};
  }
  if (nav.guidance_stale) {
    return Note(NavNoteKind::kWarning, FontRole::kEssential, "Navigation stale");
  }
  if (nav.rerouting) {
    return Note(NavNoteKind::kWarning, FontRole::kEssential, "Rerouting");
  }
  if (nav.off_route) {
    return Note(NavNoteKind::kWarning, FontRole::kEssential, "Off route");
  }
  if (state.caps.route_geometry) {
    if (!nav.has_window) {
      return Note(NavNoteKind::kWarning, FontRole::kEssential, "No route data");
    }
    const uint32_t age_ms = now_ms - nav.window_received_ms;
    if (age_ms > kPositionStaleMs) {
      std::array<char, 32> text{};
      std::snprintf(text.data(), text.size(), "Position stale %us",
                    static_cast<unsigned>(age_ms / kMsPerSecond));
      return Note(NavNoteKind::kWarning, FontRole::kEssential, text.data());
    }
  }
  const BackendState& backend = state.backend;
  if (!backend.valid) {
    return NavNote{};
  }
  if (backend.switched && now_ms - backend.switched_ms < kBackendNoticeMs) {
    std::array<char, 40> text{};
    std::snprintf(text.data(), text.size(), "Now: %s", backend.name.CStr());
    return Note(NavNoteKind::kNotice, FontRole::kSupporting, text.data());
  }
  // Attribution is too long for ride-time text sizes, so it shows only while stopped, at the
  // stopped-only size (ADR-0003; OSM allows attribution that isn't always on screen).
  if (!state.ride.moving) {
    std::array<char, 96> text{};
    std::snprintf(text.data(), text.size(), "%s\n%s", backend.name.CStr(),
                  backend.attribution.CStr());
    return Note(NavNoteKind::kAttribution, FontRole::kStoppedOnly, text.data());
  }
  return NavNote{};
}

const char* NavIdleText(const AppState& state) {
  if (state.phone.connected) {
    return "No navigation";
  }
  switch (state.phone.link) {
    case LinkStatus::kOff:
      return "Bluetooth paused";
    case LinkStatus::kSecuring:
      return "Securing link";
    case LinkStatus::kUpdateApp:
      return "Update the app";
    case LinkStatus::kUpdateFirmware:
      return "Update the firmware";
    case LinkStatus::kWaiting:
    case LinkStatus::kReady:
      break;
  }
  return "Phone not connected";
}

const char* LinkText(LinkStatus status) {
  switch (status) {
    case LinkStatus::kOff:
      return "paused";
    case LinkStatus::kWaiting:
      return "not connected";
    case LinkStatus::kSecuring:
      return "securing";
    case LinkStatus::kReady:
      return "connected";
    case LinkStatus::kUpdateApp:
      return "update the app";
    case LinkStatus::kUpdateFirmware:
      return "update firmware";
  }
  return "?";
}

}  // namespace sarathi::core
