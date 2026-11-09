// How the map follows the rider (core/map_follow): zoom by speed and near turns, hysteresis,
// dead reckoning between poses, heading easing, redraw thresholds.

#include <cmath>

#include <unity.h>

#include "map_follow.h"

using sarathi::core::MapFollow;
using sarathi::core::MapViewPoint;
using sarathi::core::RiderPose;
using sarathi::core::ZoomForSpeed;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

RiderPose Pose(float kmh, float heading, uint32_t at_ms, float x = 0, float y = 0) {
  return RiderPose{true, x, y, heading, kmh / 3.6F, at_ms};
}

// Runs the follower for `ms` in 50 ms steps with one pose; returns the last view.
MapViewPoint Run(MapFollow& f, const RiderPose& p, uint32_t from_ms, uint32_t ms, bool turn = false,
                 uint32_t to_turn_m = 0) {
  MapViewPoint v;
  for (uint32_t t = from_ms; t <= from_ms + ms; t += 50) v = f.Update(p, turn, to_turn_m, t);
  return v;
}

void test_zoom_follows_speed() {
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 2.0F, ZoomForSpeed(0));
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 2.0F, ZoomForSpeed(20));
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 6.0F, ZoomForSpeed(60));
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 10.0F, ZoomForSpeed(100));
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 10.0F, ZoomForSpeed(140));
}

void test_zoom_eases_and_zooms_in_before_a_turn() {
  MapFollow f;
  MapViewPoint v = Run(f, Pose(100, 0, 0), 0, 10000);
  TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, v.metres_per_px);
  v = Run(f, Pose(100, 0, 10000), 10000, 500, true, 250);  // half a second after: still easing
  TEST_ASSERT_TRUE(v.metres_per_px > 2.5F && v.metres_per_px < 10.0F);
  v = Run(f, Pose(100, 0, 10500), 10500, 8000, true, 250);
  TEST_ASSERT_FLOAT_WITHIN(0.1F, 2.0F, v.metres_per_px);
}

void test_small_speed_changes_dont_pump_the_zoom() {
  MapFollow f;
  MapViewPoint v = Run(f, Pose(60, 0, 0), 0, 10000);
  const float settled = v.metres_per_px;
  for (int i = 0; i < 20; ++i) {  // 57-63 km/h jitter
    v = Run(f, Pose(i % 2 == 0 ? 57.0F : 63.0F, 0, 10000 + i * 500), 10000 + i * 500, 500);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01F, settled, v.metres_per_px);
}

void test_dead_reckons_along_the_heading_for_five_seconds() {
  MapFollow f;
  const RiderPose east = Pose(36, 90, 1000, 100, 200);  // 10 m/s east
  MapViewPoint v = f.Update(east, false, 0, 1000);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 100.0F, v.x_m);
  v = f.Update(east, false, 0, 1500);
  TEST_ASSERT_FLOAT_WITHIN(0.05F, 105.0F, v.x_m);
  TEST_ASSERT_FLOAT_WITHIN(0.05F, 200.0F, v.y_m);
  v = f.Update(east, false, 0, 9000);  // 8 s later: stopped after 5 s
  TEST_ASSERT_FLOAT_WITHIN(0.05F, 150.0F, v.x_m);
}

void test_heading_turns_the_short_way_at_a_limited_rate() {
  MapFollow f;
  f.Update(Pose(30, 350, 0), false, 0, 0);
  const MapViewPoint v = f.Update(Pose(30, 10, 0), false, 0, 100);  // 20 degrees, 0.1 s: 12
  TEST_ASSERT_FLOAT_WITHIN(0.1F, 2.0F, v.heading_deg);
  const MapViewPoint later = Run(f, Pose(30, 10, 0), 100, 1000);
  TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, later.heading_deg);
}

void test_redraw_only_when_it_shows() {
  const MapViewPoint a{0, 0, 90, 2};
  TEST_ASSERT_FALSE(MapFollow::NeedsRedraw(a, MapViewPoint{1.0F, 0, 90.5F, 2}));  // 0.5 px
  TEST_ASSERT_TRUE(MapFollow::NeedsRedraw(a, MapViewPoint{2.5F, 0, 90, 2}));      // 1.25 px
  TEST_ASSERT_TRUE(MapFollow::NeedsRedraw(a, MapViewPoint{0, 0, 91.6F, 2}));
  TEST_ASSERT_TRUE(MapFollow::NeedsRedraw(a, MapViewPoint{0, 0, 90, 2.1F}));
  TEST_ASSERT_TRUE(
      MapFollow::NeedsRedraw(MapViewPoint{0, 0, 359.5F, 2}, MapViewPoint{0, 0, 1.2F, 2}));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_zoom_follows_speed);
  RUN_TEST(test_zoom_eases_and_zooms_in_before_a_turn);
  RUN_TEST(test_small_speed_changes_dont_pump_the_zoom);
  RUN_TEST(test_dead_reckons_along_the_heading_for_five_seconds);
  RUN_TEST(test_heading_turns_the_short_way_at_a_limited_rate);
  RUN_TEST(test_redraw_only_when_it_shows);
  return UNITY_END();
}
