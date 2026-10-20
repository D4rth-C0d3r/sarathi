#include <array>

#include <unity.h>

#include "maneuver_glyph.h"

using sarathi::core::GlyphFor;
using sarathi::core::kGlyphSize;
using sarathi::core::Maneuver;
using sarathi::core::ManeuverGlyph;
using sarathi::core::ManeuverType;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

constexpr std::array<ManeuverType, 11> kDrawn = {
    ManeuverType::kDepart,     ManeuverType::kStraight,   ManeuverType::kSlightLeft,
    ManeuverType::kLeft,       ManeuverType::kSharpLeft,  ManeuverType::kSlightRight,
    ManeuverType::kRight,      ManeuverType::kSharpRight, ManeuverType::kUTurn,
    ManeuverType::kRoundabout, ManeuverType::kArrive,
};

void TestEveryManeuverHasAShapeInsideTheBox() {
  for (const ManeuverType type : kDrawn) {
    const ManeuverGlyph g = GlyphFor(Maneuver{type, 2});
    TEST_ASSERT_TRUE(g.stroke_count >= 2);  // a stem and a head (or flag)
    for (size_t i = 0; i < g.stroke_count; ++i) {
      TEST_ASSERT_TRUE(g.strokes[i].count >= 2);
      for (size_t j = 0; j < g.strokes[i].count; ++j) {
        TEST_ASSERT_TRUE(g.strokes[i].points[j].x >= 0 && g.strokes[i].points[j].x <= kGlyphSize);
        TEST_ASSERT_TRUE(g.strokes[i].points[j].y >= 0 && g.strokes[i].points[j].y <= kGlyphSize);
      }
    }
  }
}

void TestLeftMirrorsRight() {
  const ManeuverGlyph left = GlyphFor(Maneuver{ManeuverType::kLeft, 0});
  const ManeuverGlyph right = GlyphFor(Maneuver{ManeuverType::kRight, 0});
  TEST_ASSERT_EQUAL_UINT8(right.stroke_count, left.stroke_count);
  for (size_t i = 0; i < right.stroke_count; ++i) {
    for (size_t j = 0; j < right.strokes[i].count; ++j) {
      TEST_ASSERT_EQUAL_INT(kGlyphSize - right.strokes[i].points[j].x, left.strokes[i].points[j].x);
      TEST_ASSERT_EQUAL_INT(right.strokes[i].points[j].y, left.strokes[i].points[j].y);
    }
  }
}

void TestRightPointsRightAndLeftPointsLeft() {
  // The arrow tip (end of the stem) is right of centre for right turns.
  const ManeuverGlyph right = GlyphFor(Maneuver{ManeuverType::kRight, 0});
  const auto& stem = right.strokes[0];
  TEST_ASSERT_TRUE(stem.points[stem.count - 1].x > kGlyphSize / 2);
  const ManeuverGlyph left = GlyphFor(Maneuver{ManeuverType::kSlightLeft, 0});
  TEST_ASSERT_TRUE(left.strokes[0].points[left.strokes[0].count - 1].x < kGlyphSize / 2);
}

void TestRoundaboutHasRingAndExitDirection() {
  const ManeuverGlyph first = GlyphFor(Maneuver{ManeuverType::kRoundabout, 1});
  const ManeuverGlyph third = GlyphFor(Maneuver{ManeuverType::kRoundabout, 3});
  TEST_ASSERT_TRUE(first.has_ring);
  TEST_ASSERT_TRUE(first.strokes[1].points[1].x > kGlyphSize / 2);  // exit 1 heads right
  TEST_ASSERT_TRUE(third.strokes[1].points[1].x < kGlyphSize / 2);  // exit 3 heads left
}

void TestNoneIsEmpty() { TEST_ASSERT_EQUAL_UINT8(0, GlyphFor(Maneuver{}).stroke_count); }

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestEveryManeuverHasAShapeInsideTheBox);
  RUN_TEST(TestLeftMirrorsRight);
  RUN_TEST(TestRightPointsRightAndLeftPointsLeft);
  RUN_TEST(TestRoundaboutHasRingAndExitDirection);
  RUN_TEST(TestNoneIsEmpty);
  return UNITY_END();
}
