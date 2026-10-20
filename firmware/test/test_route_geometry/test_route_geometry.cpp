#include <array>
#include <cmath>
#include <cstdlib>

#include <unity.h>

#include "route_geometry.h"

using sarathi::core::kMaxRoutePoints;
using sarathi::core::Maneuver;
using sarathi::core::ManeuverType;
using sarathi::core::MetricPoint;
using sarathi::core::RouteManeuver;
using sarathi::core::RoutePath;
using sarathi::core::RouteWindow;
using sarathi::core::SimplifyPolyline;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

// An L: 1000 m north, then 1000 m east, with a right turn at the corner.
constexpr std::array<MetricPoint, 3> kL = {{{0, 0}, {0, 1000}, {1000, 1000}}};
constexpr std::array<RouteManeuver, 2> kLManeuvers = {{
    {1, {ManeuverType::kRight, 0}},
    {2, {ManeuverType::kArrive, 0}},
}};

RoutePath MakeL() { return {kL.data(), kL.size(), kLManeuvers.data(), kLManeuvers.size()}; }

void TestLengthAndPoints() {
  const RoutePath p = MakeL();
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 2000.0F, p.LengthM());
  const MetricPoint mid = p.PointAt(500.0F);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, mid.x);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 500.0F, mid.y);
  const MetricPoint after = p.PointAt(1250.0F);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 250.0F, after.x);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 1000.0F, after.y);
  const MetricPoint clamped = p.PointAt(5000.0F);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 1000.0F, clamped.x);
}

void TestHeading() {
  const RoutePath p = MakeL();
  TEST_ASSERT_EQUAL_UINT16(0, p.HeadingAt(10.0F));     // north
  TEST_ASSERT_EQUAL_UINT16(90, p.HeadingAt(1500.0F));  // east
}

void TestNextManeuver() {
  const RoutePath p = MakeL();
  const RouteManeuver* m = p.NextManeuver(200.0F);
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL(ManeuverType::kRight, m->maneuver.type);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 800.0F, p.VertexDistanceM(m->vertex) - 200.0F);
  TEST_ASSERT_EQUAL(ManeuverType::kArrive, p.NextManeuver(1000.0F)->maneuver.type);
  TEST_ASSERT_NULL(p.NextManeuver(2000.0F));
}

void TestWindowIsRelativeToRider() {
  const RoutePath p = MakeL();
  RouteWindow w{};
  p.Window(800.0F, 100.0F, 500.0F, 1.0F, w);
  // From 700 m (100 m behind) through the corner to 1300 m: start, corner, end.
  TEST_ASSERT_EQUAL_UINT8(3, w.count);
  TEST_ASSERT_EQUAL_INT16(0, w.points[0].x_m);
  TEST_ASSERT_EQUAL_INT16(-100, w.points[0].y_m);  // behind the rider
  TEST_ASSERT_EQUAL_INT16(0, w.points[1].x_m);
  TEST_ASSERT_EQUAL_INT16(200, w.points[1].y_m);  // the corner, 200 m ahead
  TEST_ASSERT_EQUAL_INT16(300, w.points[2].x_m);
  TEST_ASSERT_EQUAL_INT16(200, w.points[2].y_m);
  TEST_ASSERT_EQUAL_UINT16(0, w.heading_deg);
  TEST_ASSERT_TRUE(w.has_maneuver_point);
  TEST_ASSERT_EQUAL_INT16(0, w.maneuver_point.x_m);
  TEST_ASSERT_EQUAL_INT16(200, w.maneuver_point.y_m);
}

void TestWindowClampsAtRouteEnds() {
  const RoutePath p = MakeL();
  RouteWindow w{};
  p.Window(0.0F, 100.0F, 300.0F, 1.0F, w);
  TEST_ASSERT_EQUAL_UINT8(2, w.count);
  TEST_ASSERT_EQUAL_INT16(0, w.points[0].y_m);  // can't go behind the start
  TEST_ASSERT_EQUAL_INT16(300, w.points[1].y_m);
  TEST_ASSERT_FALSE(w.has_maneuver_point);  // the corner is beyond 300 m
}

void TestSimplifyKeepsCornersDropsNoise() {
  // A straight line with 1 m wobble, then a real corner.
  const std::array<MetricPoint, 6> pts = {
      {{0, 0}, {1, 100}, {0, 200}, {-1, 300}, {0, 400}, {400, 400}}};
  std::array<bool, 6> keep{};
  const size_t kept = SimplifyPolyline(pts.data(), pts.size(), 3.0F, keep.data());
  TEST_ASSERT_EQUAL_UINT(3, kept);
  TEST_ASSERT_TRUE(keep[0]);
  TEST_ASSERT_TRUE(keep[4]);  // the corner
  TEST_ASSERT_TRUE(keep[5]);
}

void TestDenseRouteFitsWindowLimit() {
  // A zig-zag with 120 vertices 20 m apart and 15 m sideways: every vertex matters at 3 m.
  std::array<MetricPoint, 120> zig{};
  for (size_t i = 0; i < zig.size(); ++i) {
    zig[i] = {(i % 2 == 0) ? 0.0F : 15.0F, static_cast<float>(i) * 20.0F};
  }
  const RoutePath p(zig.data(), zig.size(), nullptr, 0);
  RouteWindow w{};
  p.Window(100.0F, 100.0F, 2000.0F, 3.0F, w);
  TEST_ASSERT_TRUE(w.count >= 2);
  TEST_ASSERT_TRUE(w.count <= kMaxRoutePoints);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestLengthAndPoints);
  RUN_TEST(TestHeading);
  RUN_TEST(TestNextManeuver);
  RUN_TEST(TestWindowIsRelativeToRider);
  RUN_TEST(TestWindowClampsAtRouteEnds);
  RUN_TEST(TestSimplifyKeepsCornersDropsNoise);
  RUN_TEST(TestDenseRouteFitsWindowLimit);
  return UNITY_END();
}
