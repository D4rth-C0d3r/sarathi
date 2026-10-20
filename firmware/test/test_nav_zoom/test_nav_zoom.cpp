#include <unity.h>

#include "frame_stats.h"
#include "legibility.h"
#include "nav_zoom.h"

using sarathi::core::Blend;
using sarathi::core::FrameStats;
using sarathi::core::kZoomFarMetresPerPx;
using sarathi::core::kZoomNearMetresPerPx;
using sarathi::core::NavMetresPerPx;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

void TestZoomedInNearTheTurn() {
  TEST_ASSERT_EQUAL_FLOAT(kZoomNearMetresPerPx, NavMetresPerPx(0));
  TEST_ASSERT_EQUAL_FLOAT(kZoomNearMetresPerPx, NavMetresPerPx(150));
}

void TestZoomedOutFarFromTheTurn() {
  TEST_ASSERT_EQUAL_FLOAT(kZoomFarMetresPerPx, NavMetresPerPx(1000));
  TEST_ASSERT_EQUAL_FLOAT(kZoomFarMetresPerPx, NavMetresPerPx(50000));
}

void TestLinearInBetween() {
  const float mid = NavMetresPerPx(575);  // halfway between 150 and 1000
  TEST_ASSERT_FLOAT_WITHIN(0.01F, (kZoomNearMetresPerPx + kZoomFarMetresPerPx) / 2.0F, mid);
  TEST_ASSERT_TRUE(NavMetresPerPx(300) < NavMetresPerPx(600));  // monotonic
}

void TestFrameStatsSummary() {
  FrameStats stats;
  stats.Add(10000);
  stats.Add(20000);
  stats.Add(30000);
  const FrameStats::Summary s = stats.TakeSummary();
  TEST_ASSERT_EQUAL_UINT32(3, s.count);
  TEST_ASSERT_EQUAL_UINT32(20000, s.avg_us);
  TEST_ASSERT_EQUAL_UINT32(30000, s.max_us);
  const FrameStats::Summary empty = stats.TakeSummary();  // a new period starts
  TEST_ASSERT_EQUAL_UINT32(0, empty.count);
  TEST_ASSERT_EQUAL_UINT32(0, empty.avg_us);
  TEST_ASSERT_EQUAL_UINT32(0, empty.max_us);
}

void TestBlend() {
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, Blend(0xFFFFFF, 0x000000, 100));
  TEST_ASSERT_EQUAL_HEX32(0x000000, Blend(0xFFFFFF, 0x000000, 0));
  TEST_ASSERT_EQUAL_HEX32(0x333333, Blend(0x000000, 0xFFFFFF, 80));  // 20 % of 255 = 51
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestZoomedInNearTheTurn);
  RUN_TEST(TestZoomedOutFarFromTheTurn);
  RUN_TEST(TestLinearInBetween);
  RUN_TEST(TestFrameStatsSummary);
  RUN_TEST(TestBlend);
  return UNITY_END();
}
