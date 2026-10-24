#include <cstdint>

#include <unity.h>

#include "display_policy.h"

using sarathi::core::AppState;
using sarathi::core::CallPhase;
using sarathi::core::ClockState;
using sarathi::core::DayNightBrightness;
using sarathi::core::IsNight;
using sarathi::core::ScreenPower;
using sarathi::core::ScreenTimeout;
using sarathi::core::Settings;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

ClockState At(int hour, int minute) {
  return ClockState{true, static_cast<uint16_t>((hour * 60) + minute)};
}

void TestNightBoundaries() {
  TEST_ASSERT_FALSE(IsNight(At(18, 59)));
  TEST_ASSERT_TRUE(IsNight(At(19, 0)));
  TEST_ASSERT_TRUE(IsNight(At(0, 0)));
  TEST_ASSERT_TRUE(IsNight(At(6, 59)));
  TEST_ASSERT_FALSE(IsNight(At(7, 0)));
  TEST_ASSERT_FALSE(IsNight(ClockState{}));  // no clock: never night
}

void TestDayNightBrightness() {
  Settings s;
  s.brightness_percent = 80;
  TEST_ASSERT_EQUAL_INT(80, DayNightBrightness(s, At(22, 0)));  // auto off
  s.auto_day_night = true;
  TEST_ASSERT_EQUAL_INT(80, DayNightBrightness(s, At(12, 0)));
  TEST_ASSERT_EQUAL_INT(32, DayNightBrightness(s, At(22, 0)));  // 40 % of 80
  s.brightness_percent = 10;
  TEST_ASSERT_EQUAL_INT(5, DayNightBrightness(s, At(22, 0)));  // never below the floor
}

AppState Idle(ScreenTimeout timeout) {
  AppState state;
  state.settings.screen_timeout = timeout;
  return state;
}

void TestSleepsAfterTimeoutWhenIdle() {
  ScreenPower power;
  const AppState state = Idle(ScreenTimeout::k30s);
  power.Update(state, 0);
  power.Update(state, 29999);
  TEST_ASSERT_TRUE(power.Awake());
  power.Update(state, 30000);
  TEST_ASSERT_FALSE(power.Awake());
}

// Regression: a knob step stamped after this loop's `now` (the step reads the LVGL tick) put
// the screen to sleep at once.
void TestInputStampedAfterNowDoesNotSleep() {
  ScreenPower power;
  const AppState state = Idle(ScreenTimeout::k30s);
  power.Update(state, 1000);
  TEST_ASSERT_TRUE(power.OnInput(1003));
  power.Update(state, 1000);
  TEST_ASSERT_TRUE(power.Awake());
  power.Update(state, 1003 + 30000);
  TEST_ASSERT_FALSE(power.Awake());
}

void TestNeverSleepsWithNeverTimeout() {
  ScreenPower power;
  const AppState state = Idle(ScreenTimeout::kNever);
  power.Update(state, 0);
  power.Update(state, 10U * 60U * 1000U);
  TEST_ASSERT_TRUE(power.Awake());
}

void TestNeverSleepsWhileNavigatingOrInCall() {
  ScreenPower power;
  AppState state = Idle(ScreenTimeout::k30s);
  state.nav.active = true;
  power.Update(state, 0);
  power.Update(state, 120000);
  TEST_ASSERT_TRUE(power.Awake());
  // Navigation ends: the timeout counts from then.
  state.nav.active = false;
  power.Update(state, 140000);
  TEST_ASSERT_TRUE(power.Awake());
  power.Update(state, 150000);
  TEST_ASSERT_FALSE(power.Awake());
  // An incoming call wakes the screen.
  state.call.phase = CallPhase::kRinging;
  power.Update(state, 151000);
  TEST_ASSERT_TRUE(power.Awake());
}

void TestWakingInputIsSwallowed() {
  ScreenPower power;
  const AppState state = Idle(ScreenTimeout::k30s);
  power.Update(state, 0);
  TEST_ASSERT_TRUE(power.OnInput(1000));  // awake: act on it
  power.Update(state, 30999);
  TEST_ASSERT_TRUE(power.Awake());  // the input at 1 s reset the timer
  power.Update(state, 31000);
  TEST_ASSERT_FALSE(power.Awake());
  TEST_ASSERT_FALSE(power.OnInput(40000));  // asleep: only wakes
  TEST_ASSERT_TRUE(power.Awake());
  TEST_ASSERT_TRUE(power.OnInput(40100));  // the next one acts
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestNightBoundaries);
  RUN_TEST(TestDayNightBrightness);
  RUN_TEST(TestSleepsAfterTimeoutWhenIdle);
  RUN_TEST(TestInputStampedAfterNowDoesNotSleep);
  RUN_TEST(TestNeverSleepsWithNeverTimeout);
  RUN_TEST(TestNeverSleepsWhileNavigatingOrInCall);
  RUN_TEST(TestWakingInputIsSwallowed);
  return UNITY_END();
}
