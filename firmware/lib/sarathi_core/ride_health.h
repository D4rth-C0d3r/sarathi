// The dash's health since power-on, for checks on a real ride without a laptop (Phase 4 step H,
// docs/phase4-test-plan.md): the chip's temperature, with the backlight cap that protects it
// (CLAUDE.md, Power), and how often the phone link dropped or the position went stale. Pure
// logic; the platform reads the sensor and applies the cap. Nothing is written to flash.

#pragma once

#include <cstdint>

#include "app_state.h"

namespace sarathi::core {

// The ESP32-S3's own sensor reads the die, which runs above the air around it. At kHotChipC
// (the board's 65 °C ambient limit plus about 15 °C of self-heating) the backlight is capped at
// kHotBacklightPercent, and stays capped until the chip cools below kCoolChipC. The H11 ride
// check (an hour in the sun) records the real temperatures to tune these.
constexpr int kHotChipC = 80;
constexpr int kCoolChipC = 72;
constexpr int kHotBacklightPercent = 50;

class RideHealth {
 public:
  // A sensor reading, every few seconds. `valid` false: the sensor failed or is missing (the
  // simulator); the cap is kept as it was.
  void OnChipTemp(bool valid, float celsius);

  // Call every loop: counts the edges of what the state shows.
  void Update(const AppState& state, uint32_t now_ms);

  // The backlight after the heat cap. Never raises it.
  [[nodiscard]] int CapBacklight(int percent) const;

  [[nodiscard]] bool TempValid() const { return temp_valid_; }
  [[nodiscard]] int TempC() const { return temp_c_; }
  [[nodiscard]] int MaxTempC() const { return max_temp_c_; }
  [[nodiscard]] bool Hot() const { return hot_; }
  [[nodiscard]] uint16_t HotCount() const { return hot_count_; }
  // The phone link dropped (not the planned pause for Wi-Fi).
  [[nodiscard]] uint16_t PhoneLost() const { return phone_lost_; }
  // The position or the guidance went stale while navigating ("Position stale", "Navigation
  // stale" on the Nav screen).
  [[nodiscard]] uint16_t StaleCount() const { return stale_count_; }

 private:
  bool temp_valid_ = false;
  bool any_reading_ = false;
  int temp_c_ = 0;
  int max_temp_c_ = 0;
  bool hot_ = false;
  uint16_t hot_count_ = 0;
  bool was_connected_ = false;
  bool was_stale_ = false;
  uint16_t phone_lost_ = 0;
  uint16_t stale_count_ = 0;
};

// Whether the Nav screen shows the route data as stale: guidance stopped arriving, or (with
// route geometry) no route window for longer than kPositionStaleMs.
[[nodiscard]] bool RouteDataStale(const AppState& state, uint32_t now_ms);

}  // namespace sarathi::core
