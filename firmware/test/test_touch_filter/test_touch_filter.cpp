#include <unity.h>

#include "touch_filter.h"

using sarathi::core::TouchFilter;
using sarathi::core::TouchPoint;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

constexpr int16_t kSize = 480;
constexpr int16_t kYOffset = 20;
constexpr int16_t kJitter = 4;

void TestAppliesYOffset() {
  TouchFilter f(kSize, kSize, kYOffset, kJitter);
  const TouchPoint p = f.Apply(240, 260);
  TEST_ASSERT_EQUAL_INT(240, p.x);
  TEST_ASSERT_EQUAL_INT(240, p.y);
}

void TestClampsToScreen() {
  TouchFilter f(kSize, kSize, kYOffset, kJitter);
  TouchPoint p = f.Apply(-5, 5);  // y - 20 < 0
  TEST_ASSERT_EQUAL_INT(0, p.x);
  TEST_ASSERT_EQUAL_INT(0, p.y);
  f.Release();
  p = f.Apply(600, 700);
  TEST_ASSERT_EQUAL_INT(kSize - 1, p.x);
  TEST_ASSERT_EQUAL_INT(kSize - 1, p.y);
}

void TestHoldsSmallMoves() {
  TouchFilter f(kSize, kSize, kYOffset, kJitter);
  f.Apply(100, 120);
  const TouchPoint p = f.Apply(103, 123);  // moved 3 px on both axes
  TEST_ASSERT_EQUAL_INT(100, p.x);
  TEST_ASSERT_EQUAL_INT(100, p.y);
}

void TestFollowsMovesAtThreshold() {
  TouchFilter f(kSize, kSize, kYOffset, kJitter);
  f.Apply(100, 120);
  const TouchPoint p = f.Apply(104, 120);
  TEST_ASSERT_EQUAL_INT(104, p.x);
  TEST_ASSERT_EQUAL_INT(100, p.y);
}

void TestReleaseStartsFresh() {
  TouchFilter f(kSize, kSize, kYOffset, kJitter);
  f.Apply(100, 120);
  f.Release();
  const TouchPoint p = f.Apply(102, 122);
  TEST_ASSERT_EQUAL_INT(102, p.x);
  TEST_ASSERT_EQUAL_INT(102, p.y);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestAppliesYOffset);
  RUN_TEST(TestClampsToScreen);
  RUN_TEST(TestHoldsSmallMoves);
  RUN_TEST(TestFollowsMovesAtThreshold);
  RUN_TEST(TestReleaseStartsFresh);
  return UNITY_END();
}
