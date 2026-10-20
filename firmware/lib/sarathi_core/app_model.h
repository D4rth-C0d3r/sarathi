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
  void SetClock(uint16_t minutes_of_day);  // 0-1439, wraps

  // ---- Settings from storage ----
  // Applies saved settings at boot (not subject to the moving lock; brightness clamped). Does
  // not count as a change to save.
  void LoadSettings(const Settings& settings);

  // ---- Rider actions ----
  // Return false (and do nothing) when the action doesn't apply right now.
  bool AcceptCall();      // only while ringing
  bool RejectCall();      // while ringing or active
  bool MediaPlayPause();  // only when media is available
  bool MediaNext();
  bool MediaPrev();
  // Settings: all refused while moving (CLAUDE.md).
  bool SetTheme(ThemeId theme);
  bool SetBrightness(int percent);  // clamped to 5-100 %
  bool SetAutoDayNight(bool on);
  bool SetUnits(Units units);
  bool SetScreenTimeout(ScreenTimeout timeout);
  bool SetKnobReversed(bool reversed);

  [[nodiscard]] bool SettingsLocked() const { return state_.ride.moving; }

  // Increases with every rider change to the settings, so the saver knows when to write.
  [[nodiscard]] uint32_t SettingsRevision() const { return settings_revision_; }

  [[nodiscard]] const AppState& State() const { return state_; }

  // Next queued command for the data source, oldest first.
  bool TakeCommand(Command& out);

 private:
  bool Queue(CommandType type);

  // Applies `next` unless locked; counts a revision only if something changed.
  bool ChangeSettings(const Settings& next);

  AppState state_;
  uint32_t settings_revision_ = 0;
  std::array<Command, kCommandQueueLength> queue_{};
  size_t queue_head_ = 0;
  size_t queue_count_ = 0;
};

}  // namespace sarathi::core
