// Single owner of AppState. Data comes in from a data source (the phone later, DemoRide now);
// rider actions go out as Commands. The phone stays the source of truth (ADR-0001): an action
// only queues a command, and the state changes when the source reports the result.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app_state.h"

namespace sarathi::core {

enum class CommandType : uint8_t {
  kCallAccept,
  kCallReject,  // also hangs up an active call
  kMediaPlayPause,
  kMediaNext,
  kMediaPrev,
};

struct Command {
  CommandType type = CommandType::kCallReject;
};

constexpr size_t kCommandQueueLength = 8;

class AppModel {
 public:
  // ---- From the data source ----
  void SetGuidance(const Maneuver& next, uint32_t distance_m, std::string_view street,
                   uint32_t eta_s);
  void SetRouteWindow(const RouteWindow& window, uint32_t now_ms);
  void EndNav();
  void SetCall(CallPhase phase, std::string_view caller, uint32_t now_ms);
  void SetMedia(const MediaState& media);
  void SetSpeed(uint16_t speed_kmh);

  // ---- Rider actions ----
  // Return false (and do nothing) when the action doesn't apply right now.
  bool AcceptCall();      // only while ringing
  bool RejectCall();      // while ringing or active
  bool MediaPlayPause();  // only when media is available
  bool MediaNext();
  bool MediaPrev();
  bool SetTheme(ThemeId theme);     // refused while moving
  bool SetBrightness(int percent);  // refused while moving; clamped to 5-100 %

  // Brightness and theme changes are settings, locked while moving (CLAUDE.md).
  [[nodiscard]] bool SettingsLocked() const { return state_.ride.moving; }

  [[nodiscard]] const AppState& State() const { return state_; }

  // Next queued command for the data source, oldest first.
  bool TakeCommand(Command& out);

 private:
  bool Queue(CommandType type);

  AppState state_;
  std::array<Command, kCommandQueueLength> queue_{};
  size_t queue_head_ = 0;
  size_t queue_count_ = 0;
};

}  // namespace sarathi::core
