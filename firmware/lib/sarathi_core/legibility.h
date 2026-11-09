// ADR-0003 legibility rules as code: contrast (WCAG 2 formula) and minimum font sizes. Every
// registered theme must pass (test_theme_legibility).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "theme.h"

namespace sarathi::core {

// Minimum font size per role (ADR-0003, visual angle at 75 cm on this panel). Indexed by
// FontRole. Primary is the current floor (48 px = 17'); its target is 56 px (20').
constexpr std::array<uint8_t, kFontRoleCount> kMinFontPx = {48, 46, 34, 28};

constexpr float kMinContrastEssential = 7.0F;   // primary and essential text
constexpr float kMinContrastSupporting = 4.5F;  // supporting text, route line, arrow
constexpr float kMinContrastNonText = 3.0F;     // focus ring, danger cue (WCAG non-text)

// WCAG 2 contrast ratio between two colours, 1.0 to 21.0.
[[nodiscard]] float ContrastRatio(Rgb a, Rgb b);

// Colour vision (ADR-0009): the traffic levels must stay apart for colour-blind riders.
enum class Vision : uint8_t { kNormal, kProtanopia, kDeuteranopia, kTritanopia };
// How `c` looks with `vision` (Machado, Oliveira and Fernandes 2009, severity 1.0).
[[nodiscard]] Rgb Simulate(Rgb c, Vision vision);
// CIE76 colour difference (Lab, D65): about 2.3 is just noticeable; 15 is plainly different.
[[nodiscard]] float DeltaE(Rgb a, Rgb b);
constexpr float kMinTrafficDeltaE = 15.0F;

// `top` drawn at `top_percent` opacity over `bottom` (per-channel, like LVGL's blending).
[[nodiscard]] Rgb Blend(Rgb top, Rgb bottom, uint8_t top_percent);

struct LegibilityIssue {
  const char* rule = "";
  float value = 0.0F;
  float required = 0.0F;
};

constexpr size_t kMaxLegibilityIssues = 32;

struct LegibilityReport {
  std::array<LegibilityIssue, kMaxLegibilityIssues> issues{};
  size_t count = 0;

  [[nodiscard]] bool Passed() const { return count == 0; }
};

// Checks every rule that can be checked from data alone.
[[nodiscard]] LegibilityReport CheckLegibility(const ThemeDescriptor& theme);

}  // namespace sarathi::core
