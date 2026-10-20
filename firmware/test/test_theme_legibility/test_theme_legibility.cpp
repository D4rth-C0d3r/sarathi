#include <cstdio>

#include <unity.h>

#include "legibility.h"
#include "ornament.h"
#include "theme_registry.h"

using sarathi::core::BuildOrnament;
using sarathi::core::CheckLegibility;
using sarathi::core::ContrastRatio;
using sarathi::core::InOrnamentZone;
using sarathi::core::IsRegistered;
using sarathi::core::kClassicTheme;
using sarathi::core::LegibilityReport;
using sarathi::core::Ornament;
using sarathi::core::OrnamentColor;
using sarathi::core::OrnamentLine;
using sarathi::core::OrnamentRect;
using sarathi::core::OrnamentShapes;
using sarathi::core::ThemeAt;
using sarathi::core::ThemeCount;
using sarathi::core::ThemeDescriptor;
using sarathi::core::ThemeFor;
using sarathi::core::ThemeId;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

void TestContrastFormula() {
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 21.0F, ContrastRatio(0xFFFFFF, 0x000000));
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 1.0F, ContrastRatio(0x777777, 0x777777));
  TEST_ASSERT_FLOAT_WITHIN(0.05F, 4.48F, ContrastRatio(0x777777, 0xFFFFFF));  // WCAG example
  TEST_ASSERT_FLOAT_WITHIN(0.01F, ContrastRatio(0x123456, 0xABCDEF),
                           ContrastRatio(0xABCDEF, 0x123456));  // symmetric
}

void TestEveryRegisteredThemeIsLegible() {
  TEST_ASSERT_TRUE(ThemeCount() >= 1);
  for (size_t i = 0; i < ThemeCount(); ++i) {
    const ThemeDescriptor& theme = ThemeAt(i);
    const LegibilityReport report = CheckLegibility(theme);
    for (size_t j = 0; j < report.count; ++j) {
      std::printf("%s: %s = %.2f, needs %.2f\n", theme.name, report.issues[j].rule,
                  static_cast<double>(report.issues[j].value),
                  static_cast<double>(report.issues[j].required));
    }
    TEST_ASSERT_TRUE_MESSAGE(report.Passed(), theme.name);
  }
}

void TestCheckerCatchesProblems() {
  ThemeDescriptor bad = kClassicTheme;
  bad.palette.text_primary = 0x555555;  // grey on black: about 2.7:1
  bad.fonts.px[0] = 30;                 // primary below 48 px, and not a built-in size
  const LegibilityReport report = CheckLegibility(bad);
  TEST_ASSERT_FALSE(report.Passed());
  TEST_ASSERT_TRUE(report.count >= 3);
}

void TestRegistry() {
  TEST_ASSERT_EQUAL(ThemeId::kClassic, ThemeAt(0).id);  // default first
  TEST_ASSERT_TRUE(IsRegistered(ThemeId::kClassic));
  TEST_ASSERT_TRUE(IsRegistered(ThemeId::kFighterHud));
  TEST_ASSERT_TRUE(IsRegistered(ThemeId::kRacing));
  TEST_ASSERT_EQUAL(3, ThemeCount());
  const auto unknown = static_cast<ThemeId>(200);
  TEST_ASSERT_FALSE(IsRegistered(unknown));
  TEST_ASSERT_EQUAL(ThemeId::kClassic, ThemeFor(unknown).id);  // falls back
  TEST_ASSERT_EQUAL(ThemeId::kFighterHud, ThemeFor(ThemeId::kFighterHud).id);
  TEST_ASSERT_EQUAL(ThemeId::kRacing, ThemeFor(ThemeId::kRacing).id);
  TEST_ASSERT_EQUAL(ThemeId::kClassic, ThemeAt(99).id);
}

void TestThemesHaveTheirOrnaments() {
  TEST_ASSERT_EQUAL(Ornament::kNone, ThemeFor(ThemeId::kClassic).ornament);
  TEST_ASSERT_EQUAL(Ornament::kHudReticle, ThemeFor(ThemeId::kFighterHud).ornament);
  TEST_ASSERT_EQUAL(Ornament::kRacingTach, ThemeFor(ThemeId::kRacing).ornament);
  const OrnamentShapes none = BuildOrnament(Ornament::kNone);
  TEST_ASSERT_EQUAL(0, none.line_count + none.rect_count);
  TEST_ASSERT_TRUE(BuildOrnament(Ornament::kHudReticle).line_count > 0);
  TEST_ASSERT_TRUE(BuildOrnament(Ornament::kRacingTach).rect_count > 0);
}

void TestOrnamentZone() {
  // Inside: the right strip at the horizon, and its mirror image.
  TEST_ASSERT_TRUE(InOrnamentZone(OrnamentLine{197, 0, 212, 0, 2, OrnamentColor::kAccent}));
  TEST_ASSERT_TRUE(InOrnamentZone(OrnamentLine{-197, 0, -212, 0, 2, OrnamentColor::kAccent}));
  TEST_ASSERT_TRUE(InOrnamentZone(OrnamentRect{198, -3, 10, 6, OrnamentColor::kPrimary}));
  // Into the text column, past the ring, across the middle, too far up, or empty.
  TEST_ASSERT_FALSE(InOrnamentZone(OrnamentLine{190, 0, 205, 0, 2, OrnamentColor::kAccent}));
  TEST_ASSERT_FALSE(InOrnamentZone(OrnamentLine{200, 0, 214, 0, 2, OrnamentColor::kAccent}));
  TEST_ASSERT_FALSE(InOrnamentZone(OrnamentLine{-200, 0, 200, 0, 2, OrnamentColor::kAccent}));
  TEST_ASSERT_FALSE(InOrnamentZone(OrnamentLine{197, -90, 197, 0, 2, OrnamentColor::kAccent}));
  TEST_ASSERT_FALSE(InOrnamentZone(OrnamentRect{205, 0, 12, 6, OrnamentColor::kPrimary}));
  TEST_ASSERT_FALSE(InOrnamentZone(OrnamentRect{198, 0, 0, 6, OrnamentColor::kPrimary}));
}

void TestCheckerCatchesThickRing() {
  ThemeDescriptor bad = kClassicTheme;
  bad.components.ring_width_px = 24;  // would reach into the ornament zone
  TEST_ASSERT_FALSE(CheckLegibility(bad).Passed());
}

// Prints each theme's contrast pairs, for the step report (pio test -v).
void PrintContrastTable() {
  for (size_t i = 0; i < ThemeCount(); ++i) {
    const ThemeDescriptor& t = ThemeAt(i);
    const auto& p = t.palette;
    const auto card =
        sarathi::core::Blend(p.background, p.route_line, sarathi::core::kCardOpacityPercent);
    std::printf(
        "%s | primary/bg %.1f | primary/surface %.1f | supporting/bg %.1f | "
        "supporting/surface %.1f | warning/bg %.1f | primary/card %.1f | supporting/card "
        "%.1f | warning/card %.1f | route/bg %.1f | arrow/bg %.1f | accept %.1f | reject "
        "%.1f | neutral %.1f | focus/bg %.1f | gauge/surface %.1f | danger/bg %.1f\n",
        t.name, static_cast<double>(ContrastRatio(p.text_primary, p.background)),
        static_cast<double>(ContrastRatio(p.text_primary, p.surface)),
        static_cast<double>(ContrastRatio(p.text_supporting, p.background)),
        static_cast<double>(ContrastRatio(p.text_supporting, p.surface)),
        static_cast<double>(ContrastRatio(p.warning, p.background)),
        static_cast<double>(ContrastRatio(p.text_primary, card)),
        static_cast<double>(ContrastRatio(p.text_supporting, card)),
        static_cast<double>(ContrastRatio(p.warning, card)),
        static_cast<double>(ContrastRatio(p.route_line, p.background)),
        static_cast<double>(ContrastRatio(p.maneuver, p.background)),
        static_cast<double>(ContrastRatio(p.button_text, p.fill_accept)),
        static_cast<double>(ContrastRatio(p.button_text, p.fill_reject)),
        static_cast<double>(ContrastRatio(p.button_text, p.fill_neutral)),
        static_cast<double>(ContrastRatio(p.accent, p.background)),
        static_cast<double>(ContrastRatio(p.accent, p.surface)),
        static_cast<double>(ContrastRatio(p.danger, p.background)));
  }
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestContrastFormula);
  RUN_TEST(TestEveryRegisteredThemeIsLegible);
  RUN_TEST(TestCheckerCatchesProblems);
  RUN_TEST(TestRegistry);
  RUN_TEST(TestThemesHaveTheirOrnaments);
  RUN_TEST(TestOrnamentZone);
  RUN_TEST(TestCheckerCatchesThickRing);
  PrintContrastTable();
  return UNITY_END();
}
