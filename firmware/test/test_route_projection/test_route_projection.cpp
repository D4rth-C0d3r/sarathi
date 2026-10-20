#include <cmath>
#include <cstdlib>

#include <unity.h>

#include "route_projection.h"

using sarathi::core::ProjectedRoute;
using sarathi::core::ProjectionParams;
using sarathi::core::ProjectRoute;
using sarathi::core::RouteWindow;
using sarathi::core::ScreenPoint;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

// 1 m per px, rider at the centre of a large circle, so nothing clips unless a test wants it.
ProjectionParams Params(float radius = 1000.0F) {
  ProjectionParams p;
  p.metres_per_px = 1.0F;
  p.rider = {240, 240};
  p.circle_center = {240, 240};
  p.circle_radius_px = radius;
  return p;
}

RouteWindow Window(std::initializer_list<sarathi::core::RoutePoint> points, uint16_t heading) {
  RouteWindow w;
  for (const auto& p : points) {
    w.points[w.count++] = p;
  }
  w.heading_deg = heading;
  return w;
}

float Distance(ScreenPoint p, ScreenPoint c) {
  return std::hypot(static_cast<float>(p.x - c.x), static_cast<float>(p.y - c.y));
}

void TestNorthUpWhenHeadingNorth() {
  ProjectedRoute out;
  ProjectRoute(Window({{0, -50}, {0, 0}, {0, 100}}, 0), Params(), out);
  // Split at the rider: behind (down the screen) and ahead (up the screen).
  TEST_ASSERT_EQUAL_UINT(2, out.count);
  TEST_ASSERT_TRUE(out.segments[0].behind);
  TEST_ASSERT_EQUAL_INT16(290, out.segments[0].a.y);  // 50 m behind = 50 px below the rider
  TEST_ASSERT_FALSE(out.segments[1].behind);
  TEST_ASSERT_EQUAL_INT16(140, out.segments[1].b.y);  // 100 m ahead = 100 px above
  TEST_ASSERT_EQUAL_INT16(240, out.segments[1].b.x);
}

void TestHeadingUpRotation() {
  ProjectedRoute out;
  // Riding east (90 deg): a point 100 m east is straight ahead, i.e. up the screen.
  ProjectRoute(Window({{0, 0}, {100, 0}}, 90), Params(), out);
  TEST_ASSERT_EQUAL_UINT(1, out.count);
  TEST_ASSERT_EQUAL_INT16(240, out.segments[0].b.x);
  TEST_ASSERT_EQUAL_INT16(140, out.segments[0].b.y);
  // A point 100 m north is then on the rider's left.
  ProjectRoute(Window({{0, 0}, {0, 100}}, 90), Params(), out);
  TEST_ASSERT_EQUAL_INT16(140, out.segments[0].b.x);
  TEST_ASSERT_EQUAL_INT16(240, out.segments[0].b.y);
}

void TestScale() {
  ProjectedRoute out;
  ProjectionParams p = Params();
  p.metres_per_px = 4.0F;
  ProjectRoute(Window({{0, 0}, {0, 400}}, 0), p, out);
  TEST_ASSERT_EQUAL_INT16(140, out.segments[0].b.y);  // 400 m at 4 m/px = 100 px
}

void TestClipsToCircle() {
  ProjectedRoute out;
  // 1 km ahead with a 200 px circle: the line ends on the circle edge.
  ProjectRoute(Window({{0, 0}, {0, 1000}}, 0), Params(200.0F), out);
  TEST_ASSERT_EQUAL_UINT(1, out.count);
  TEST_ASSERT_FLOAT_WITHIN(1.5F, 200.0F, Distance(out.segments[0].b, {240, 240}));
}

void TestDropsSegmentsOutsideAndSplitsReentry() {
  ProjectedRoute out;
  // Ahead, out past the edge, along outside, and back in.
  ProjectRoute(Window({{0, 0}, {0, 300}, {300, 300}, {300, 0}, {100, 0}}, 0), Params(200.0F), out);
  // Segment 2 (300,300)-(300,0) at x = 300 lies fully outside the 200 px circle.
  for (size_t i = 0; i < out.count; ++i) {
    TEST_ASSERT_TRUE(Distance(out.segments[i].a, {240, 240}) <= 201.0F);
    TEST_ASSERT_TRUE(Distance(out.segments[i].b, {240, 240}) <= 201.0F);
  }
  TEST_ASSERT_EQUAL_UINT(2, out.count);  // the first leg and the leg coming back in
}

void TestManeuverMarker() {
  ProjectedRoute out;
  RouteWindow w = Window({{0, 0}, {0, 200}}, 0);
  w.has_maneuver_point = true;
  w.maneuver_point = {0, 150};
  ProjectRoute(w, Params(), out);
  TEST_ASSERT_TRUE(out.has_maneuver);
  TEST_ASSERT_EQUAL_INT16(90, out.maneuver.y);
  ProjectRoute(w, Params(100.0F), out);  // 150 px away: outside a 100 px circle
  TEST_ASSERT_FALSE(out.has_maneuver);
}

void TestEmptyWindow() {
  ProjectedRoute out;
  out.count = 5;
  ProjectRoute(RouteWindow{}, Params(), out);
  TEST_ASSERT_EQUAL_UINT(0, out.count);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestNorthUpWhenHeadingNorth);
  RUN_TEST(TestHeadingUpRotation);
  RUN_TEST(TestScale);
  RUN_TEST(TestClipsToCircle);
  RUN_TEST(TestDropsSegmentsOutsideAndSplitsReentry);
  RUN_TEST(TestManeuverMarker);
  RUN_TEST(TestEmptyWindow);
  return UNITY_END();
}
