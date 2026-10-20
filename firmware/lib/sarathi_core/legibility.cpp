#include "legibility.h"

#include <algorithm>
#include <cmath>

namespace sarathi::core {
namespace {

float Channel(uint32_t value) {
  const float c = static_cast<float>(value & 0xFFU) / 255.0F;
  return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
}

float Luminance(Rgb rgb) {
  return (0.2126F * Channel(rgb >> 16U)) + (0.7152F * Channel(rgb >> 8U)) +
         (0.0722F * Channel(rgb));
}

class Checker {
 public:
  explicit Checker(LegibilityReport& report) : report_(report) {}

  void Contrast(const char* rule, Rgb fg, Rgb bg, float required) {
    Require(rule, ContrastRatio(fg, bg), required);
  }

  void Require(const char* rule, float value, float required) {
    if (value + 1e-4F < required && report_.count < report_.issues.size()) {
      report_.issues[report_.count++] = LegibilityIssue{rule, value, required};
    }
  }

 private:
  LegibilityReport& report_;
};

}  // namespace

float ContrastRatio(Rgb a, Rgb b) {
  const float la = Luminance(a);
  const float lb = Luminance(b);
  return (std::max(la, lb) + 0.05F) / (std::min(la, lb) + 0.05F);
}

LegibilityReport CheckLegibility(const ThemeDescriptor& theme) {
  LegibilityReport report;
  Checker check(report);
  const ThemePalette& p = theme.palette;

  check.Contrast("primary text on background", p.text_primary, p.background, kMinContrastEssential);
  check.Contrast("primary text on surface", p.text_primary, p.surface, kMinContrastEssential);
  check.Contrast("supporting text on background", p.text_supporting, p.background,
                 kMinContrastSupporting);
  check.Contrast("supporting text on surface", p.text_supporting, p.surface,
                 kMinContrastSupporting);
  check.Contrast("warning text on background", p.warning, p.background, kMinContrastSupporting);
  check.Contrast("route line on background", p.route_line, p.background, kMinContrastSupporting);
  check.Contrast("maneuver arrow on background", p.maneuver, p.background, kMinContrastSupporting);
  check.Contrast("rider marker on background", p.rider, p.background, kMinContrastSupporting);
  check.Contrast("button text on accept", p.button_text, p.fill_accept, kMinContrastEssential);
  check.Contrast("button text on reject", p.button_text, p.fill_reject, kMinContrastEssential);
  check.Contrast("button text on neutral", p.button_text, p.fill_neutral, kMinContrastEssential);
  check.Contrast("focus ring on background", p.accent, p.background, kMinContrastNonText);
  check.Contrast("gauge indicator on surface", p.accent, p.surface, kMinContrastNonText);
  check.Contrast("danger cue on background", p.danger, p.background, kMinContrastNonText);

  for (size_t role = 0; role < kFontRoleCount; ++role) {
    const uint8_t px = theme.fonts.px.at(role);
    check.Require("font size", px, kMinFontPx.at(role));
    const bool built_in =
        std::find(kBuiltInFontPx.begin(), kBuiltInFontPx.end(), px) != kBuiltInFontPx.end();
    check.Require("font size is a built-in Montserrat size", built_in ? 1.0F : 0.0F, 1.0F);
  }

  // Focus must be visible; the ring and the route need some width.
  check.Require("focus outline width", theme.components.focus_outline_px, 2.0F);
  check.Require("route line width", theme.components.route_line_px, 3.0F);
  check.Require("ring gauge width", theme.components.ring_width_px, 3.0F);
  return report;
}

}  // namespace sarathi::core
