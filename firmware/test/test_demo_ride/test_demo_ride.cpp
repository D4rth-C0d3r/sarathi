#include <cstdint>

#include <unity.h>

#include "app_model.h"
#include "demo_ride.h"

using sarathi::core::AppModel;
using sarathi::core::CallPhase;
using sarathi::core::Command;
using sarathi::core::DemoRide;
using sarathi::core::ManeuverType;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

constexpr uint32_t kTickMs = 20;

// Runs the ride for `duration_ms` of wall time from `now`, forwarding commands like the app.
void Run(DemoRide& ride, AppModel& model, uint32_t& now, uint32_t duration_ms) {
  for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += kTickMs, now += kTickMs) {
    ride.Poll(now, model);
    Command c;
    while (model.TakeCommand(c)) {
      ride.OnCommand(c, now, model);
    }
  }
}

void TestStartsNavigating() {
  DemoRide ride;
  AppModel model;
  uint32_t now = 1000;
  Run(ride, model, now, kTickMs);
  const auto& nav = model.State().nav;
  TEST_ASSERT_TRUE(nav.active);
  TEST_ASSERT_EQUAL(ManeuverType::kRight, nav.next.type);
  TEST_ASSERT_EQUAL_UINT32(400, nav.distance_m);
  TEST_ASSERT_EQUAL_STRING("Station Road", nav.street.CStr());
  TEST_ASSERT_TRUE(nav.has_window);
  TEST_ASSERT_TRUE(nav.window.count >= 2);
  TEST_ASSERT_EQUAL_UINT16(0, nav.window.heading_deg);  // starts heading north
  TEST_ASSERT_TRUE(model.State().ride.moving);
}

void TestMovesAtScriptedSpeed() {
  DemoRide ride;
  AppModel model;
  uint32_t now = 0;
  Run(ride, model, now, 10000);  // 10 s at 40 km/h = 111 m
  TEST_ASSERT_FLOAT_WITHIN(2.0F, 111.1F, ride.PositionM());
  TEST_ASSERT_EQUAL_UINT16(40, model.State().ride.speed_kmh);
}

void TestStopsAtTrafficLightAndUnlocksSettings() {
  DemoRide ride;
  AppModel model;
  uint32_t now = 0;
  Run(ride, model, now, 38000);  // reaches 400 m at 36 s, then waits 10 s
  TEST_ASSERT_FLOAT_WITHIN(0.5F, 400.0F, ride.PositionM());
  TEST_ASSERT_FALSE(model.State().ride.moving);
  TEST_ASSERT_FALSE(model.SettingsLocked());
  Run(ride, model, now, 10000);
  TEST_ASSERT_TRUE(model.State().ride.moving);
  TEST_ASSERT_EQUAL(ManeuverType::kLeft, model.State().nav.next.type);  // past the right turn
}

void TestWindowRefreshesEverySecond() {
  DemoRide ride;
  AppModel model;
  uint32_t now = 0;
  Run(ride, model, now, kTickMs);
  const uint32_t first = model.State().nav.window.sequence;
  Run(ride, model, now, 5000);
  TEST_ASSERT_UINT32_WITHIN(1, first + 5, model.State().nav.window.sequence);
}

void TestCallRingsAndIsMissed() {
  DemoRide ride;
  AppModel model;
  uint32_t now = 0;
  Run(ride, model, now, 59000);
  TEST_ASSERT_EQUAL(CallPhase::kIdle, model.State().call.phase);
  Run(ride, model, now, 2000);
  TEST_ASSERT_EQUAL(CallPhase::kRinging, model.State().call.phase);
  TEST_ASSERT_EQUAL_STRING("Demo Caller", model.State().call.caller.CStr());
  Run(ride, model, now, 21000);
  TEST_ASSERT_EQUAL(CallPhase::kIdle, model.State().call.phase);
}

void TestAcceptedCallGoesActiveThenEnds() {
  DemoRide ride;
  AppModel model;
  uint32_t now = 0;
  Run(ride, model, now, 61000);
  TEST_ASSERT_TRUE(model.AcceptCall());
  Run(ride, model, now, kTickMs);
  TEST_ASSERT_EQUAL(CallPhase::kActive, model.State().call.phase);
  Run(ride, model, now, 26000);
  TEST_ASSERT_EQUAL(CallPhase::kIdle, model.State().call.phase);
}

void TestRejectedCallEnds() {
  DemoRide ride;
  AppModel model;
  uint32_t now = 0;
  Run(ride, model, now, 61000);
  TEST_ASSERT_TRUE(model.RejectCall());
  Run(ride, model, now, kTickMs);
  TEST_ASSERT_EQUAL(CallPhase::kIdle, model.State().call.phase);
}

void TestMediaControls() {
  DemoRide ride;
  AppModel model;
  uint32_t now = 0;
  Run(ride, model, now, kTickMs);
  TEST_ASSERT_TRUE(model.State().media.playing);
  TEST_ASSERT_EQUAL_STRING("Open Road", model.State().media.title.CStr());
  TEST_ASSERT_TRUE(model.MediaNext());
  Run(ride, model, now, kTickMs);
  TEST_ASSERT_EQUAL_STRING("Night Ride", model.State().media.title.CStr());
  TEST_ASSERT_TRUE(model.MediaPrev());
  TEST_ASSERT_TRUE(model.MediaPrev());
  Run(ride, model, now, kTickMs);
  TEST_ASSERT_EQUAL_STRING("Long Way Home", model.State().media.title.CStr());  // wraps
  TEST_ASSERT_TRUE(model.MediaPlayPause());
  Run(ride, model, now, kTickMs);
  TEST_ASSERT_FALSE(model.State().media.playing);
}

void TestPauseStopsTheBike() {
  DemoRide ride;
  AppModel model;
  uint32_t now = 0;
  Run(ride, model, now, 5000);
  const float before = ride.PositionM();
  ride.SetPaused(true);
  Run(ride, model, now, 5000);
  TEST_ASSERT_FLOAT_WITHIN(0.01F, before, ride.PositionM());
  TEST_ASSERT_FALSE(model.State().ride.moving);
  // Route windows keep arriving while stopped, so the map never looks stale.
  const uint32_t sequence = model.State().nav.window.sequence;
  Run(ride, model, now, 3000);
  TEST_ASSERT_UINT32_WITHIN(1, sequence + 3, model.State().nav.window.sequence);
  TEST_ASSERT_TRUE(now - model.State().nav.window_received_ms <= 1000);
  ride.SetPaused(false);
  Run(ride, model, now, 1000);
  TEST_ASSERT_TRUE(ride.PositionM() > before);
}

void TestArrivesAndLoops() {
  DemoRide ride;
  ride.SetSpeedFactor(10);
  AppModel model;
  uint32_t now = 0;
  bool arrived = false;
  bool restarted = false;
  for (int i = 0; i < 2000 && !restarted; ++i) {  // up to 400 s of ride time
    Run(ride, model, now, kTickMs * 10);
    if (model.State().nav.next.type == ManeuverType::kArrive && model.State().nav.distance_m == 0) {
      arrived = true;
    } else if (arrived && ride.PositionM() < 100.0F) {
      restarted = true;
    }
  }
  TEST_ASSERT_TRUE(arrived);
  TEST_ASSERT_TRUE(restarted);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestStartsNavigating);
  RUN_TEST(TestMovesAtScriptedSpeed);
  RUN_TEST(TestStopsAtTrafficLightAndUnlocksSettings);
  RUN_TEST(TestWindowRefreshesEverySecond);
  RUN_TEST(TestCallRingsAndIsMissed);
  RUN_TEST(TestAcceptedCallGoesActiveThenEnds);
  RUN_TEST(TestRejectedCallEnds);
  RUN_TEST(TestMediaControls);
  RUN_TEST(TestPauseStopsTheBike);
  RUN_TEST(TestArrivesAndLoops);
  return UNITY_END();
}
