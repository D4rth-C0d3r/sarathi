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
  kCallReject,   // also hangs up an active call
  kCallDismiss,  // hides the call on the dash only; the phone keeps ringing
  kMediaPlayPause,
  kMediaNext,
  kMediaPrev,
  kPairAccept,    // the code matches: bond with the phone
  kPairReject,    // the code doesn't match, or the rider didn't ask to pair
  kForgetPhones,  // delete every stored bond
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
  // Guidance as the phone formats it (ADR-0005). Keeps the current icon.
  void SetPhoneGuidance(std::string_view distance, std::string_view instruction,
                        std::string_view remaining);
  void SetManeuverIcon(const ManeuverIcon& icon);
  void SetRouteWindow(const RouteWindow& window, uint32_t now_ms);
  void EndNav();
  void SetCall(CallPhase phase, std::string_view caller, uint32_t now_ms);
  void SetMedia(const MediaState& media);
  void SetSpeed(uint16_t speed_kmh);  // moving at kMovingThresholdKmh, no hysteresis (demo)
  // Speed and moving decided by the source (MovingDetector, with hysteresis).
  void SetMotion(uint16_t speed_kmh, bool moving);
  void SetSpeedValid(bool valid) { state_.ride.speed_valid = valid; }
  void SetLockEnabled(bool enabled) { state_.ride.lock_enabled = enabled; }
  void SetClock(uint16_t minutes_of_day);  // 0-1439, wraps
  void SetCapabilities(const Capabilities& caps) { state_.caps = caps; }
  void SetPhoneConnected(bool connected) { state_.phone.connected = connected; }
  void SetLinkStatus(LinkStatus status) { state_.phone.link = status; }
  void SetBackend(const BackendState& backend) { state_.backend = backend; }
  // Only while navigating; EndNav clears them.
  void SetNavFlags(bool rerouting, bool off_route);
  void SetGuidanceStale(bool stale);
  void ClearRouteWindow() {
    state_.nav.has_window = false;
    state_.nav.window = RouteWindow{};
  }
  void SetPhoneBattery(uint8_t percent, bool charging);
  void SetPairedPhones(uint8_t count) { state_.phone.paired_phones = count; }
  void SetPairingRequest(uint32_t code, uint32_t now_ms);
  void EndPairingRequest() { state_.pairing = PairingState{}; }

  // ---- Settings from storage ----
  // Applies saved settings at boot (not subject to the moving lock; brightness clamped). Does
  // not count as a change to save.
  void LoadSettings(const Settings& settings);

  // ---- Rider actions ----
  // Return false (and do nothing) when the action doesn't apply right now.
  // Call and media actions also need the data source to support them (Capabilities).
  bool AcceptCall();      // only while ringing
  bool RejectCall();      // while ringing or active
  bool DismissCall();     // only while ringing, when rejecting isn't supported
  bool MediaPlayPause();  // only when media is available
  bool MediaNext();
  bool MediaPrev();
  // Pairing: only while a phone is asking. Either ends the request on the dash.
  bool AcceptPairing();
  bool RejectPairing();
  // Settings-locked, like the settings themselves; only with phone_pairing.
  bool ForgetPhones();
  // Settings: all refused while moving (CLAUDE.md).
  bool SetTheme(ThemeId theme);
  bool SetBrightness(int percent);  // clamped to 5-100 %
  bool SetAutoDayNight(bool on);
  bool SetUnits(Units units);
  bool SetScreenTimeout(ScreenTimeout timeout);
  bool SetKnobReversed(bool reversed);

  // Locked while moving. Without a speed source, or while its speed isn't arriving, locked
  // while navigating (ADR-0005). Never locked when the rider turned the lock off (ADR-0011).
  [[nodiscard]] bool SettingsLocked() const {
    const RideState& ride = state_.ride;
    const bool no_speed = !state_.caps.speed || !ride.speed_valid;
    return ride.lock_enabled && (ride.moving || (no_speed && state_.nav.active));
  }

  // Increases with every rider change to the settings, so the saver knows when to write.
  [[nodiscard]] uint32_t SettingsRevision() const { return settings_revision_; }

  [[nodiscard]] const AppState& State() const { return state_; }

  // Next queued command for the data source, oldest first.
  bool TakeCommand(Command& out);

 private:
  bool Queue(CommandType type);
  bool MediaAction(CommandType type);
  bool EndPairingWith(CommandType type);

  // Applies `next` unless locked; counts a revision only if something changed.
  bool ChangeSettings(const Settings& next);

  AppState state_;
  uint32_t settings_revision_ = 0;
  std::array<Command, kCommandQueueLength> queue_{};
  size_t queue_head_ = 0;
  size_t queue_count_ = 0;
};

}  // namespace sarathi::core
