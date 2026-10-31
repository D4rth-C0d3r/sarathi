// Route tracker (core/route_tracker): following the rider along a received route and cutting
// the route window the Nav map draws.

#include <cmath>
#include <cstdint>
#include <vector>

#include <unity.h>

#include "route_tracker.h"

using sarathi::core::kMaxRoutePoints;
using sarathi::core::Maneuver;
using sarathi::core::ManeuverType;
using sarathi::core::MetricPoint;
using sarathi::core::RouteFix;
using sarathi::core::RouteTracker;
using sarathi::core::RouteWindow;
using sarathi::core::TrackerStorage;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

struct Grid {
  int32_t x;
  int32_t y;
};

constexpr size_t kCapacity = 4000;
std::vector<MetricPoint> g_points(kCapacity);
std::vector<float> g_cumulative(kCapacity);

RouteTracker MakeTracker() {
  return RouteTracker(TrackerStorage{g_points.data(), g_cumulative.data(), kCapacity});
}

// North 400 m, then east 300 m, in 1 m quanta... scaled from decimetres.
const std::vector<Grid> kEll = {{0, 0}, {0, 4000}, {3000, 4000}};

void TestSetRouteScalesAndMeasures() {
  RouteTracker t = MakeTracker();
  TEST_ASSERT_TRUE(t.SetRoute(kEll.data(), kEll.size(), 0.1F));
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 700.0F, t.LengthM());
  TEST_ASSERT_EQUAL(0, t.HeadingAt(100.0F));
  TEST_ASSERT_EQUAL(90, t.HeadingAt(500.0F));
  TEST_ASSERT_FALSE(t.SetRoute(kEll.data(), 1, 0.1F));
  TEST_ASSERT_FALSE(t.HasRoute());
}

void TestLocateFollowsAndRecovers() {
  RouteTracker t = MakeTracker();
  TEST_ASSERT_TRUE(t.SetRoute(kEll.data(), kEll.size(), 0.1F));
  RouteFix fix = t.Locate({5.0F, 200.0F});
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 200.0F, fix.along_m);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 5.0F, fix.offset_m);
  fix = t.Locate({150.0F, 403.0F});  // past the corner
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 550.0F, fix.along_m);
  // Far off the route (a detour): the whole route is searched.
  fix = t.Locate({-80.0F, 10.0F});
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 10.0F, fix.along_m);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 80.0F, fix.offset_m);
}

void TestWindowIsRelativeToTheRider() {
  RouteTracker t = MakeTracker();
  TEST_ASSERT_TRUE(t.SetRoute(kEll.data(), kEll.size(), 0.1F));
  TEST_ASSERT_TRUE(t.AddManeuver(1, Maneuver{ManeuverType::kRight, 0}));
  TEST_ASSERT_TRUE(t.AddManeuver(2, Maneuver{ManeuverType::kArrive, 0}));
  TEST_ASSERT_FALSE(t.AddManeuver(2, Maneuver{}));  // not increasing
  const MetricPoint rider{2.0F, 300.0F};
  const RouteFix fix = t.Locate(rider);
  RouteWindow w;
  t.Window(rider, fix, 7, 100.0F, 1500.0F, 3.0F, w);
  TEST_ASSERT_EQUAL(7, w.heading_deg);
  TEST_ASSERT_EQUAL(3, w.count);  // 100 m behind, the corner, the end
  TEST_ASSERT_EQUAL(-2, w.points[0].x_m);
  TEST_ASSERT_EQUAL(-100, w.points[0].y_m);
  TEST_ASSERT_EQUAL(-2, w.points[1].x_m);
  TEST_ASSERT_EQUAL(100, w.points[1].y_m);
  TEST_ASSERT_EQUAL(298, w.points[2].x_m);
  TEST_ASSERT_TRUE(w.has_maneuver_point);
  TEST_ASSERT_EQUAL(100, w.maneuver_point.y_m);  // the corner, the next maneuver
}

// A dense route (4,000 points, 1 m apart, gently curving) still fits the window.
void TestDenseRouteIsThinned() {
  std::vector<Grid> dense(kCapacity);
  for (size_t i = 0; i < dense.size(); ++i) {
    const float s = static_cast<float>(i);
    dense[i] = {static_cast<int32_t>(std::lround(200.0F * std::sin(s / 300.0F) * 10.0F)),
                static_cast<int32_t>(i * 10)};
  }
  RouteTracker t = MakeTracker();
  TEST_ASSERT_TRUE(t.SetRoute(dense.data(), dense.size(), 0.1F));
  const MetricPoint rider{0.0F, 1000.0F};
  const RouteFix fix = t.Locate({200.0F * std::sin(1000.0F / 300.0F), 1000.0F});
  RouteWindow w;
  t.Window(rider, fix, 0, 100.0F, 1500.0F, 3.0F, w);
  TEST_ASSERT_TRUE(w.count >= 10);
  TEST_ASSERT_TRUE(w.count <= kMaxRoutePoints);
  // The far end is about 1,500 m ahead along the route (y grows 1 m per point).
  TEST_ASSERT_INT_WITHIN(60, 1500 - 120, w.points[w.count - 1].y_m);
  TEST_ASSERT_FALSE(w.has_maneuver_point);
}

void TestNoRouteNoWindow() {
  RouteTracker t = MakeTracker();
  RouteWindow w;
  w.count = 5;
  t.Window({0, 0}, RouteFix{}, 0, 100.0F, 1500.0F, 3.0F, w);
  TEST_ASSERT_EQUAL(0, w.count);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, t.Locate({1.0F, 1.0F}).along_m);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestSetRouteScalesAndMeasures);
  RUN_TEST(TestLocateFollowsAndRecovers);
  RUN_TEST(TestWindowIsRelativeToTheRider);
  RUN_TEST(TestDenseRouteIsThinned);
  RUN_TEST(TestNoRouteNoWindow);
  return UNITY_END();
}
