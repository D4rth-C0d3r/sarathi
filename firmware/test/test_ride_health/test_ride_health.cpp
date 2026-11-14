// The dash's health since power-on (core/ride_health): the heat cap on the backlight with its
// hysteresis, the maximum temperature, and the counts of phone drops and stale route data.

#include <cmath>
#include <cstdint>

#include <unity.h>

#include "app_state.h"
#include "nav_status.h"
#include "ride_health.h"

using sarathi::core::AppState;
using sarathi::core::kCoolChipC;
using sarathi::core::kHotBacklightPercent;
using sarathi::core::kHotChipC;
using sarathi::core::kPositionStaleMs;
using sarathi::core::LinkStatus;
using sarathi::core::RideHealth;
using sarathi::core::RouteDataStale;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

constexpr uint32_t kNow = 100000;

AppState Connected() {
  AppState s;
  s.phone.connected = true;
  s.phone.link = LinkStatus::kReady;
  return s;
}

AppState Navigating(uint32_t window_ms) {
  AppState s = Connected();
  s.nav.active = true;
  s.nav.has_window = true;
  s.nav.window_received_ms = window_ms;
  return s;
}

void test_no_reading_no_cap() {
  RideHealth h;
  TEST_ASSERT_FALSE(h.TempValid());
  TEST_ASSERT_EQUAL(100, h.CapBacklight(100));
}

void test_hot_caps_and_cools_with_hysteresis() {
  RideHealth h;
  h.OnChipTemp(true, static_cast<float>(kHotChipC) - 0.6F);  // rounds below the limit
  TEST_ASSERT_FALSE(h.Hot());
  TEST_ASSERT_EQUAL(100, h.CapBacklight(100));
  h.OnChipTemp(true, static_cast<float>(kHotChipC));
  TEST_ASSERT_TRUE(h.Hot());
  TEST_ASSERT_EQUAL(kHotBacklightPercent, h.CapBacklight(100));
  TEST_ASSERT_EQUAL(30, h.CapBacklight(30));           // never raises a lower setting
  h.OnChipTemp(true, static_cast<float>(kCoolChipC));  // not yet below
  TEST_ASSERT_TRUE(h.Hot());
  h.OnChipTemp(true, static_cast<float>(kCoolChipC - 1));
  TEST_ASSERT_FALSE(h.Hot());
  TEST_ASSERT_EQUAL(100, h.CapBacklight(100));
  h.OnChipTemp(true, static_cast<float>(kHotChipC + 3));
  TEST_ASSERT_EQUAL(2, h.HotCount());
}

void test_failed_reading_keeps_cap_and_max() {
  RideHealth h;
  h.OnChipTemp(true, 85.0F);
  h.OnChipTemp(false, 0.0F);
  TEST_ASSERT_FALSE(h.TempValid());
  TEST_ASSERT_TRUE(h.Hot());
  TEST_ASSERT_EQUAL(85, h.MaxTempC());
  h.OnChipTemp(true, NAN);
  TEST_ASSERT_FALSE(h.TempValid());
}

void test_max_follows_readings() {
  RideHealth h;
  h.OnChipTemp(true, 41.4F);
  TEST_ASSERT_EQUAL(41, h.MaxTempC());
  h.OnChipTemp(true, 63.5F);
  h.OnChipTemp(true, 50.0F);
  TEST_ASSERT_EQUAL(50, h.TempC());
  TEST_ASSERT_EQUAL(64, h.MaxTempC());
}

void test_phone_drop_counts_but_wifi_pause_does_not() {
  RideHealth h;
  AppState s = Connected();
  h.Update(s, kNow);
  s.phone.connected = false;
  s.phone.link = LinkStatus::kWaiting;
  h.Update(s, kNow);
  h.Update(s, kNow + 1);  // still gone: one drop
  TEST_ASSERT_EQUAL(1, h.PhoneLost());

  s = Connected();
  h.Update(s, kNow + 2);
  s.phone.connected = false;
  s.phone.link = LinkStatus::kOff;  // BLE paused for an update check
  h.Update(s, kNow + 3);
  TEST_ASSERT_EQUAL(1, h.PhoneLost());
}

void test_stale_episodes_count_once_each() {
  RideHealth h;
  AppState s = Navigating(kNow);
  h.Update(s, kNow + kPositionStaleMs);  // exactly at the limit: fresh
  TEST_ASSERT_EQUAL(0, h.StaleCount());
  h.Update(s, kNow + kPositionStaleMs + 1);
  h.Update(s, kNow + kPositionStaleMs + 500);
  TEST_ASSERT_EQUAL(1, h.StaleCount());
  s.nav.window_received_ms = kNow + 6000;  // a window arrives: fresh again
  h.Update(s, kNow + 6000);
  s.nav.guidance_stale = true;
  h.Update(s, kNow + 6001);
  TEST_ASSERT_EQUAL(2, h.StaleCount());
}

void test_route_data_stale_rules() {
  AppState s = Navigating(kNow);
  TEST_ASSERT_TRUE(RouteDataStale(s, kNow + kPositionStaleMs + 1));
  s.caps.route_geometry = false;  // arrow mode: windows don't matter
  TEST_ASSERT_FALSE(RouteDataStale(s, kNow + kPositionStaleMs + 1));
  s.nav.guidance_stale = true;
  TEST_ASSERT_TRUE(RouteDataStale(s, kNow));
  s.nav.active = false;
  TEST_ASSERT_FALSE(RouteDataStale(s, kNow));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_reading_no_cap);
  RUN_TEST(test_hot_caps_and_cools_with_hysteresis);
  RUN_TEST(test_failed_reading_keeps_cap_and_max);
  RUN_TEST(test_max_follows_readings);
  RUN_TEST(test_phone_drop_counts_but_wifi_pause_does_not);
  RUN_TEST(test_stale_episodes_count_once_each);
  RUN_TEST(test_route_data_stale_rules);
  return UNITY_END();
}
