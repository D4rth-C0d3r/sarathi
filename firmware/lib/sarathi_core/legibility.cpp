#include "legibility.h"

#include <algorithm>
#include <cmath>

#include "map_theme.h"
#include "ornament.h"

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

Rgb Blend(Rgb top, Rgb bottom, uint8_t top_percent) {
  const uint32_t a = std::min<uint32_t>(top_percent, 100U);
  Rgb out = 0;
  for (uint32_t shift = 0; shift <= 16; shift += 8) {
    const uint32_t t = (top >> shift) & 0xFFU;
    const uint32_t b = (bottom >> shift) & 0xFFU;
    out |= (((t * a) + (b * (100U - a)) + 50U) / 100U) << shift;
  }
  return out;
}

namespace {

float ToLinear(float c) {
  return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
}

float ToSrgb(float c) {
  c = std::clamp(c, 0.0F, 1.0F);
  return c <= 0.0031308F ? c * 12.92F : 1.055F * std::pow(c, 1.0F / 2.4F) - 0.055F;
}

struct Linear {
  float r, g, b;
};

Linear Split(Rgb c) {
  constexpr float kMax = 255.0F;
  return Linear{ToLinear(static_cast<float>((c >> 16U) & 0xFFU) / kMax),
                ToLinear(static_cast<float>((c >> 8U) & 0xFFU) / kMax),
                ToLinear(static_cast<float>(c & 0xFFU) / kMax)};
}

float LabF(float t) {
  constexpr float kE = 216.0F / 24389.0F;
  constexpr float kK = 24389.0F / 27.0F;
  return t > kE ? std::cbrt(t) : (kK * t + 16.0F) / 116.0F;
}

std::array<float, 3> Lab(Rgb c) {
  const Linear l = Split(c);
  // sRGB D65 to XYZ, normalised by the white point.
  const float x = (0.4124F * l.r + 0.3576F * l.g + 0.1805F * l.b) / 0.95047F;
  const float y = 0.2126F * l.r + 0.7152F * l.g + 0.0722F * l.b;
  const float z = (0.0193F * l.r + 0.1192F * l.g + 0.9505F * l.b) / 1.08883F;
  return {116.0F * LabF(y) - 16.0F, 500.0F * (LabF(x) - LabF(y)), 200.0F * (LabF(y) - LabF(z))};
}

}  // namespace

Rgb Simulate(Rgb c, Vision vision) {
  using M = std::array<float, 9>;
  static constexpr M kProtan = {0.152286F, 1.052583F,  -0.204868F, 0.114503F, 0.786281F,
                                0.099216F, -0.003882F, -0.048116F, 1.051998F};
  static constexpr M kDeutan = {0.367322F, 0.860646F,  -0.227968F, 0.280085F, 0.672501F,
                                0.047413F, -0.011820F, 0.042940F,  0.968881F};
  static constexpr M kTritan = {1.255528F, -0.076749F, -0.178779F, -0.078411F, 0.930809F,
                                0.147602F, 0.004733F,  0.691367F,  0.303900F};
  if (vision == Vision::kNormal) {
    return c;
  }
  const M& m = vision == Vision::kProtanopia     ? kProtan
               : vision == Vision::kDeuteranopia ? kDeutan
                                                 : kTritan;
  const Linear l = Split(c);
  const auto out = [&](size_t row) {
    const float v = m.at(row * 3) * l.r + m.at(row * 3 + 1) * l.g + m.at(row * 3 + 2) * l.b;
    return static_cast<Rgb>(std::lround(ToSrgb(v) * 255.0F));
  };
  return (out(0) << 16U) | (out(1) << 8U) | out(2);
}

float DeltaE(Rgb a, Rgb b) {
  const auto la = Lab(a);
  const auto lb = Lab(b);
  return std::hypot(la[0] - lb[0], la[1] - lb[1], la[2] - lb[2]);
}

// The map (ADR-0009): the route at every traffic level stands out from the ground (and from the
// roads when there's no casing between them), the levels stay apart for colour-blind riders, and
// each level has its own pattern or width.
void CheckMap(const ThemeDescriptor& theme, Checker& check) {
  const ThemeMapStyle& m = theme.map;
  constexpr std::array<TrafficLevel, 3> kColoured = {TrafficLevel::kFree, TrafficLevel::kSlow,
                                                     TrafficLevel::kJam};
  constexpr std::array<const char*, 3> kNames = {
      "route (free) on map ground", "slow traffic on map ground", "jam traffic on map ground"};
  for (size_t i = 0; i < kColoured.size(); ++i) {
    const Rgb c = RouteLookFor(theme, kColoured.at(i), false).color;
    check.Contrast(kNames.at(i), c, m.ground, kMinContrastSupporting);
    if (m.route_casing_px == 0) {
      for (const Rgb road : m.road) {
        check.Contrast("route on roads (no casing)", c, road, kMinContrastNonText);
      }
    }
  }
  check.Contrast("unknown traffic on map ground",
                 RouteLookFor(theme, TrafficLevel::kUnknown, false).color, m.ground,
                 kMinContrastNonText);
  for (const Vision v :
       {Vision::kNormal, Vision::kProtanopia, Vision::kDeuteranopia, Vision::kTritanopia}) {
    for (size_t i = 0; i < kColoured.size(); ++i) {
      for (size_t j = i + 1; j < kColoured.size(); ++j) {
        const Rgb a = Simulate(RouteLookFor(theme, kColoured.at(i), false).color, v);
        const Rgb b = Simulate(RouteLookFor(theme, kColoured.at(j), false).color, v);
        check.Require("traffic colours apart (incl. colour-blind)", DeltaE(a, b),
                      kMinTrafficDeltaE);
      }
    }
  }
  // Never by colour alone: no two levels share both pattern and width.
  constexpr std::array<TrafficLevel, 4> kAll = {TrafficLevel::kFree, TrafficLevel::kSlow,
                                                TrafficLevel::kJam, TrafficLevel::kUnknown};
  for (size_t i = 0; i < kAll.size(); ++i) {
    for (size_t j = i + 1; j < kAll.size(); ++j) {
      const RouteLook a = RouteLookFor(theme, kAll.at(i), false);
      const RouteLook b = RouteLookFor(theme, kAll.at(j), false);
      const bool same = a.pattern == b.pattern && a.width_px == b.width_px;
      check.Require("traffic levels differ in pattern or width", same ? 0.0F : 1.0F, 1.0F);
    }
  }
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
  // Warnings are text needed to act (ADR-0003), so they need the essential contrast.
  check.Contrast("warning text on background", p.warning, p.background, kMinContrastEssential);
  check.Contrast("route line on background", p.route_line, p.background, kMinContrastSupporting);
  check.Contrast("maneuver arrow on background", p.maneuver, p.background, kMinContrastSupporting);
  check.Contrast("rider marker on background", p.rider, p.background, kMinContrastSupporting);
  check.Contrast("button text on accept", p.button_text, p.fill_accept, kMinContrastEssential);
  check.Contrast("button text on reject", p.button_text, p.fill_reject, kMinContrastEssential);
  check.Contrast("button text on neutral", p.button_text, p.fill_neutral, kMinContrastEssential);
  check.Contrast("focus ring on background", p.accent, p.background, kMinContrastNonText);
  check.Contrast("gauge indicator on surface", p.accent, p.surface, kMinContrastNonText);
  check.Contrast("danger cue on background", p.danger, p.background, kMinContrastNonText);

  // Text on a map card, where the route line shows through (worst case).
  const Rgb card_over_route = Blend(p.background, p.route_line, kCardOpacityPercent);
  check.Contrast("primary text on card over route", p.text_primary, card_over_route,
                 kMinContrastEssential);
  check.Contrast("supporting text on card over route", p.text_supporting, card_over_route,
                 kMinContrastSupporting);
  check.Contrast("warning text on card over route", p.warning, card_over_route,
                 kMinContrastEssential);

  CheckMap(theme, check);

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

  // Ornaments stay in their zone, clear of all content (ornament.h), and the ring gauge leaves
  // room for them.
  const OrnamentShapes shapes = BuildOrnament(theme.ornament);
  for (size_t i = 0; i < shapes.line_count; ++i) {
    check.Require("ornament line inside the ornament zone",
                  InOrnamentZone(shapes.lines.at(i)) ? 1.0F : 0.0F, 1.0F);
  }
  for (size_t i = 0; i < shapes.rect_count; ++i) {
    check.Require("ornament rect inside the ornament zone",
                  InOrnamentZone(shapes.rects.at(i)) ? 1.0F : 0.0F, 1.0F);
  }
  check.Require("ring gauge width headroom before the ornament zone",
                static_cast<float>(kOrnamentMaxRingWidthPx - theme.components.ring_width_px), 0.0F);
  return report;
}

}  // namespace sarathi::core
