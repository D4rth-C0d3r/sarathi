#include <unity.h>

#include "backlight_level.h"

using sarathi::core::BacklightDuty;
using sarathi::core::ClampBacklightPercent;
using sarathi::core::kMinBacklightPercent;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

void TestClampsToMinimum() {
  TEST_ASSERT_EQUAL_INT(kMinBacklightPercent, ClampBacklightPercent(0));
  TEST_ASSERT_EQUAL_INT(kMinBacklightPercent, ClampBacklightPercent(-20));
}

void TestClampsToMaximum() { TEST_ASSERT_EQUAL_INT(100, ClampBacklightPercent(150)); }

void TestMatchesElecrowDefault() {
  // Elecrow's demo starts at duty 204 of 255, which is 80 %.
  TEST_ASSERT_EQUAL_UINT32(204, BacklightDuty(80, 8));
}

void TestEndpoints() {
  TEST_ASSERT_EQUAL_UINT32(255, BacklightDuty(100, 8));
  TEST_ASSERT_EQUAL_UINT32(12, BacklightDuty(5, 8));
  TEST_ASSERT_EQUAL_UINT32(12, BacklightDuty(0, 8));  // clamped to 5 %, never dark
}

void TestOtherResolution() { TEST_ASSERT_EQUAL_UINT32(1023, BacklightDuty(100, 10)); }

static_assert(BacklightDuty(80, 8) == 204, "usable at compile time");

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestClampsToMinimum);
  RUN_TEST(TestClampsToMaximum);
  RUN_TEST(TestMatchesElecrowDefault);
  RUN_TEST(TestEndpoints);
  RUN_TEST(TestOtherResolution);
  return UNITY_END();
}
