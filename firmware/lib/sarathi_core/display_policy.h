// What the backlight should do: day/night dimming and the screen timeout. Pure logic; the
// platform applies the result.

#pragma once

#include <cstdint>

#include "app_state.h"

namespace sarathi::core {

constexpr uint16_t kNightStartMinute = 19 * 60;  // 19:00
constexpr uint16_t kNightEndMinute = 7 * 60;     // 07:00
constexpr int kNightBrightnessPercent = 40;      // of the brightness setting

// Night is 19:00 to 07:00. Without a valid clock it is never night.
[[nodiscard]] bool IsNight(const ClockState& clock);

// The brightness setting, or the night level when auto day/night is on and it is night.
// Never below the 5 % floor.
[[nodiscard]] int DayNightBrightness(const Settings& settings, const ClockState& clock);

// Screen timeout. The screen never sleeps while navigating or during a call (an incoming call
// wakes it), and the input that wakes it only wakes it.
class ScreenPower {
 public:
  // Call for every knob step, button press and touch start. Returns false if the input only
  // woke the screen and must not be acted on.
  bool OnInput(uint32_t now_ms);

  // Call every loop.
  void Update(const AppState& state, uint32_t now_ms);

  [[nodiscard]] bool Awake() const { return awake_; }

 private:
  bool awake_ = true;
  bool started_ = false;
  uint32_t last_activity_ms_ = 0;
};

}  // namespace sarathi::core
