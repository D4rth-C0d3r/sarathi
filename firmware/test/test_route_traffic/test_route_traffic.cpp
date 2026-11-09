// The route window split by traffic (core/route_traffic).

#include <unity.h>

#include "route_traffic.h"

using sarathi::core::kTrafficStaleMs;
using sarathi::core::RouteWindow;
using sarathi::core::SplitRouteByTraffic;
using sarathi::core::TrafficLevel;
using sarathi::core::TrafficPiece;
using sarathi::core::TrafficState;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

// North 0..1000 m, then east to 1000,1000: along 5000..6000..7000 m from the route's start.
RouteWindow Window() {
  RouteWindow w;
  w.count = 3;
  w.points.at(0) = {0, 0};
  w.points.at(1) = {0, 1000};
  w.points.at(2) = {1000, 1000};
  w.along_m.at(0) = 5000;
  w.along_m.at(1) = 6000;
  w.along_m.at(2) = 7000;
  return w;
}

TrafficState Traffic(uint32_t received_ms) {
  TrafficState t;
  t.valid = true;
  t.received_ms = received_ms;
  t.count = 2;
  t.offset_m.at(0) = 4000;  // free to 5500
  t.length_m.at(0) = 1500;
  t.level.at(0) = TrafficLevel::kFree;
  t.offset_m.at(1) = 5500;  // jam to 6500
  t.length_m.at(1) = 1000;
  t.level.at(1) = TrafficLevel::kJam;
  return t;  // 6500 onwards: unknown
}

void test_no_traffic_is_the_plain_route() {
  TrafficPiece p[16];
  const size_t n = SplitRouteByTraffic(Window(), TrafficState{}, 0, p, 16);
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_TRUE(p[0].level == TrafficLevel::kFree && p[1].level == TrafficLevel::kFree);
}

void test_split_at_stretch_boundaries() {
  TrafficPiece p[16];
  const size_t n = SplitRouteByTraffic(Window(), Traffic(1000), 2000, p, 16);
  TEST_ASSERT_EQUAL(4, n);  // free 5000-5500, jam 5500-6000 | jam 6000-6500, unknown 6500-7000
  TEST_ASSERT_TRUE(p[0].level == TrafficLevel::kFree);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 500.0F, p[0].by_m);
  TEST_ASSERT_TRUE(p[1].level == TrafficLevel::kJam);
  TEST_ASSERT_TRUE(p[2].level == TrafficLevel::kJam);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 500.0F, p[2].bx_m);
  TEST_ASSERT_TRUE(p[3].level == TrafficLevel::kUnknown);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, 1000.0F, p[3].bx_m);
}

void test_old_traffic_is_unknown() {
  TrafficPiece p[16];
  const size_t n = SplitRouteByTraffic(Window(), Traffic(1000), 1000 + kTrafficStaleMs + 1, p, 16);
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_TRUE(p[0].level == TrafficLevel::kUnknown && p[1].level == TrafficLevel::kUnknown);
}

void test_capacity_is_respected() {
  TrafficPiece p[3];
  TEST_ASSERT_EQUAL(3, SplitRouteByTraffic(Window(), Traffic(0), 0, p, 3));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_traffic_is_the_plain_route);
  RUN_TEST(test_split_at_stretch_boundaries);
  RUN_TEST(test_old_traffic_is_unknown);
  RUN_TEST(test_capacity_is_respected);
  return UNITY_END();
}
