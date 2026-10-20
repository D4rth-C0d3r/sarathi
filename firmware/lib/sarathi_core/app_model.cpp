#include "app_model.h"

#include "backlight_level.h"

namespace sarathi::core {

void AppModel::SetGuidance(const Maneuver& next, uint32_t distance_m, std::string_view street,
                           uint32_t eta_s) {
  state_.nav.active = true;
  state_.nav.next = next;
  state_.nav.distance_m = distance_m;
  state_.nav.street.Assign(street);
  state_.nav.eta_s = eta_s;
}

void AppModel::SetRouteWindow(const RouteWindow& window, uint32_t now_ms) {
  state_.nav.window = window;
  state_.nav.has_window = true;
  state_.nav.window_received_ms = now_ms;
}

void AppModel::EndNav() { state_.nav = NavState{}; }

void AppModel::SetCall(CallPhase phase, std::string_view caller, uint32_t now_ms) {
  if (phase != state_.call.phase) {
    state_.call.phase_since_ms = now_ms;
  }
  state_.call.phase = phase;
  state_.call.caller.Assign(phase == CallPhase::kIdle ? std::string_view{} : caller);
}

void AppModel::SetMedia(const MediaState& media) { state_.media = media; }

void AppModel::SetSpeed(uint16_t speed_kmh) {
  state_.ride.speed_kmh = speed_kmh;
  state_.ride.moving = speed_kmh >= kMovingThresholdKmh;
}

bool AppModel::AcceptCall() {
  return state_.call.phase == CallPhase::kRinging && Queue(CommandType::kCallAccept);
}

bool AppModel::RejectCall() {
  return state_.call.phase != CallPhase::kIdle && Queue(CommandType::kCallReject);
}

bool AppModel::MediaPlayPause() {
  return state_.media.available && Queue(CommandType::kMediaPlayPause);
}

bool AppModel::MediaNext() { return state_.media.available && Queue(CommandType::kMediaNext); }

bool AppModel::MediaPrev() { return state_.media.available && Queue(CommandType::kMediaPrev); }

void AppModel::SetClock(uint16_t minutes_of_day) {
  state_.clock.valid = true;
  state_.clock.minutes_of_day = static_cast<uint16_t>(minutes_of_day % (24U * 60U));
}

void AppModel::LoadSettings(const Settings& settings) {
  state_.settings = settings;
  state_.settings.brightness_percent = ClampBacklightPercent(settings.brightness_percent);
}

bool AppModel::ChangeSettings(const Settings& next) {
  if (SettingsLocked()) {
    return false;
  }
  if (next != state_.settings) {
    state_.settings = next;
    ++settings_revision_;
  }
  return true;
}

bool AppModel::SetTheme(ThemeId theme) {
  Settings next = state_.settings;
  next.theme = theme;
  return ChangeSettings(next);
}

bool AppModel::SetBrightness(int percent) {
  Settings next = state_.settings;
  next.brightness_percent = ClampBacklightPercent(percent);
  return ChangeSettings(next);
}

bool AppModel::SetAutoDayNight(bool on) {
  Settings next = state_.settings;
  next.auto_day_night = on;
  return ChangeSettings(next);
}

bool AppModel::SetUnits(Units units) {
  Settings next = state_.settings;
  next.units = units;
  return ChangeSettings(next);
}

bool AppModel::SetScreenTimeout(ScreenTimeout timeout) {
  Settings next = state_.settings;
  next.screen_timeout = timeout;
  return ChangeSettings(next);
}

bool AppModel::SetKnobReversed(bool reversed) {
  Settings next = state_.settings;
  next.knob_reversed = reversed;
  return ChangeSettings(next);
}

bool AppModel::TakeCommand(Command& out) {
  if (queue_count_ == 0) {
    return false;
  }
  out = queue_[queue_head_];
  queue_head_ = (queue_head_ + 1) % kCommandQueueLength;
  --queue_count_;
  return true;
}

bool AppModel::Queue(CommandType type) {
  if (queue_count_ == kCommandQueueLength) {
    return false;  // full: drop the new command rather than an older one
  }
  queue_[(queue_head_ + queue_count_) % kCommandQueueLength] = Command{type};
  ++queue_count_;
  return true;
}

}  // namespace sarathi::core
