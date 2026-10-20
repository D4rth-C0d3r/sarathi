#include "display_policy.h"

#include "backlight_level.h"

namespace sarathi::core {

bool IsNight(const ClockState& clock) {
  if (!clock.valid) {
    return false;
  }
  return clock.minutes_of_day >= kNightStartMinute || clock.minutes_of_day < kNightEndMinute;
}

int DayNightBrightness(const Settings& settings, const ClockState& clock) {
  const int day = ClampBacklightPercent(settings.brightness_percent);
  if (!settings.auto_day_night || !IsNight(clock)) {
    return day;
  }
  return ClampBacklightPercent(day * kNightBrightnessPercent / 100);
}

bool ScreenPower::OnInput(uint32_t now_ms) {
  last_activity_ms_ = now_ms;
  if (!awake_) {
    awake_ = true;
    return false;  // this input only woke the screen
  }
  return true;
}

void ScreenPower::Update(const AppState& state, uint32_t now_ms) {
  if (!started_) {
    started_ = true;
    last_activity_ms_ = now_ms;
  }
  const uint32_t timeout_ms = ScreenTimeoutMs(state.settings.screen_timeout);
  const bool busy = state.nav.active || state.call.phase != CallPhase::kIdle;
  if (timeout_ms == 0 || busy) {
    // Keep counting from the end of the navigation or call, not from the last input.
    last_activity_ms_ = now_ms;
    awake_ = true;
    return;
  }
  if (now_ms - last_activity_ms_ >= timeout_ms) {
    awake_ = false;
  }
}

}  // namespace sarathi::core
