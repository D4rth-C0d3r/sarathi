#include <vector>

#include <unity.h>

#include "maneuver_icon.h"

using sarathi::core::ForEachIconRun;
using sarathi::core::IconPixel;
using sarathi::core::IconRun;
using sarathi::core::kManeuverIconBytes;
using sarathi::core::kManeuverIconSize;
using sarathi::core::ManeuverIcon;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

std::vector<IconRun> Runs(const ManeuverIcon& icon) {
  std::vector<IconRun> runs;
  ForEachIconRun(icon, [&](const IconRun& r) { runs.push_back(r); });
  return runs;
}

void TestBitOrderIsMsbFirstRowMajor() {
  ManeuverIcon icon;
  icon.bits[0] = 0x80;  // (0, 0)
  icon.bits[6] = 0x01;  // row 1 starts at bit 48 = byte 6; 0x01 is x 7
  TEST_ASSERT_TRUE(IconPixel(icon, 0, 0));
  TEST_ASSERT_FALSE(IconPixel(icon, 1, 0));
  TEST_ASSERT_TRUE(IconPixel(icon, 7, 1));
}

void TestEmptyIconHasNoRuns() { TEST_ASSERT_EQUAL_UINT(0, Runs(ManeuverIcon{}).size()); }

void TestRunsSplitOnGapsAndRows() {
  ManeuverIcon icon;
  icon.bits[0] = 0xF0;                       // row 0: x 0-3
  icon.bits[1] = 0x0F;                       // row 0: x 12-15
  icon.bits[kManeuverIconBytes - 1] = 0x03;  // row 47: x 46-47
  const auto runs = Runs(icon);
  TEST_ASSERT_EQUAL_UINT(3, runs.size());
  TEST_ASSERT_EQUAL_UINT8(0, runs[0].x);
  TEST_ASSERT_EQUAL_UINT8(4, runs[0].length);
  TEST_ASSERT_EQUAL_UINT8(12, runs[1].x);
  TEST_ASSERT_EQUAL_UINT8(4, runs[1].length);
  TEST_ASSERT_EQUAL_UINT8(47, runs[2].y);
  TEST_ASSERT_EQUAL_UINT8(46, runs[2].x);
  TEST_ASSERT_EQUAL_UINT8(2, runs[2].length);
}

void TestFullIconIsOneRunPerRow() {
  ManeuverIcon icon;
  icon.bits.fill(0xFF);
  const auto runs = Runs(icon);
  TEST_ASSERT_EQUAL_UINT(kManeuverIconSize, runs.size());
  for (const IconRun& r : runs) {
    TEST_ASSERT_EQUAL_UINT8(0, r.x);
    TEST_ASSERT_EQUAL_UINT8(kManeuverIconSize, r.length);
  }
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestBitOrderIsMsbFirstRowMajor);
  RUN_TEST(TestEmptyIconHasNoRuns);
  RUN_TEST(TestRunsSplitOnGapsAndRows);
  RUN_TEST(TestFullIconIsOneRunPerRow);
  return UNITY_END();
}
