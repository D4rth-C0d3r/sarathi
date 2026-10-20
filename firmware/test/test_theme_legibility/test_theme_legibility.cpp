#include <cstdio>

#include <unity.h>

#include "legibility.h"
#include "theme_registry.h"

using sarathi::core::CheckLegibility;
using sarathi::core::ContrastRatio;
using sarathi::core::IsRegistered;
using sarathi::core::kClassicTheme;
using sarathi::core::LegibilityReport;
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
  TEST_ASSERT_FALSE(IsRegistered(ThemeId::kRacing));                    // not yet
  TEST_ASSERT_EQUAL(ThemeId::kClassic, ThemeFor(ThemeId::kRacing).id);  // falls back
  TEST_ASSERT_EQUAL(ThemeId::kFighterHud, ThemeFor(ThemeId::kFighterHud).id);
  TEST_ASSERT_EQUAL(ThemeId::kClassic, ThemeAt(99).id);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestContrastFormula);
  RUN_TEST(TestEveryRegisteredThemeIsLegible);
  RUN_TEST(TestCheckerCatchesProblems);
  RUN_TEST(TestRegistry);
  return UNITY_END();
}
