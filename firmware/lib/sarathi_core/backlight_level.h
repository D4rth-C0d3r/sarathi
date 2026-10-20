// Backlight percentage to PWM duty. Pure logic.

#pragma once

#include <algorithm>
#include <cstdint>

namespace sarathi::core {

// At 0 % the screen is invisible, so the user can't go below this (CLAUDE.md).
constexpr int kMinBacklightPercent = 5;
constexpr int kMaxBacklightPercent = 100;

constexpr int ClampBacklightPercent(int percent) {
  return std::clamp(percent, kMinBacklightPercent, kMaxBacklightPercent);
}

// Same mapping as Elecrow's demo, (percent * max) / 100, after clamping.
constexpr uint32_t BacklightDuty(int percent, uint8_t resolution_bits) {
  const uint32_t max_duty = (1U << resolution_bits) - 1U;
  return static_cast<uint32_t>(ClampBacklightPercent(percent)) * max_duty / 100U;
}

}  // namespace sarathi::core
