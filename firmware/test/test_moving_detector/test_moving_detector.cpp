// The moving lock's hysteresis (core/moving_detector, protocol.md section 5).

#include <cstdint>

#include <unity.h>

#include "moving_detector.h"

using sarathi::core::kSpeedTimeoutMs;
using sarathi::core::LockConfig;
using sarathi::core::MovingDetector;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

// Feeds `kmh` once a second from `from_ms` for `seconds`; returns the time after.
uint32_t Feed(MovingDetector& d, uint16_t kmh, uint32_t from_ms, uint32_t seconds) {
  uint32_t t = from_ms;
  for (uint32_t i = 0; i <= seconds; ++i, t += 1000) {
    d.Update(kmh, t);
  }
  return t;
}

void TestMovingNeedsTwoSecondsAboveFive() {
  MovingDetector d;
  d.Update(6, 0);
  d.Update(6, 1000);
  TEST_ASSERT_FALSE(d.Moving());
  d.Update(6, 2000);
  TEST_ASSERT_TRUE(d.Moving());
}

void TestJitterAtAStandstillDoesntCount() {
  MovingDetector d;
  for (uint32_t t = 0; t < 20000; t += 1000) {
    d.Update((t / 1000) % 2 == 0 ? 7 : 1, t);  // a GPS spike every other second
  }
  TEST_ASSERT_FALSE(d.Moving());
}

void TestStoppingNeedsFiveSecondsBelowThree() {
  MovingDetector d;
  uint32_t t = Feed(d, 30, 0, 3);
  TEST_ASSERT_TRUE(d.Moving());
  t = Feed(d, 4, t, 10);  // slow traffic: between the thresholds, still moving
  TEST_ASSERT_TRUE(d.Moving());
  d.Update(2, t);
  d.Update(2, t + 4000);
  TEST_ASSERT_TRUE(d.Moving());
  d.Update(2, t + 5000);
  TEST_ASSERT_FALSE(d.Moving());
}

void TestThresholdsFromThePhone() {
  MovingDetector d;
  d.Configure(LockConfig{true, 10, 0, 2, 0});
  d.Update(9, 0);
  TEST_ASSERT_FALSE(d.Moving());
  d.Update(10, 100);  // move_s 0: at once
  TEST_ASSERT_TRUE(d.Moving());
  d.Update(1, 200);
  TEST_ASSERT_FALSE(d.Moving());
}

void TestSamplesThatStopAreStale() {
  MovingDetector d;
  TEST_ASSERT_FALSE(d.Fresh(0));
  d.Update(30, 1000);
  TEST_ASSERT_TRUE(d.Fresh(1000 + kSpeedTimeoutMs - 1));
  TEST_ASSERT_FALSE(d.Fresh(1000 + kSpeedTimeoutMs));
  Feed(d, 30, 0, 3);
  d.Reset();
  TEST_ASSERT_FALSE(d.Moving());
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestMovingNeedsTwoSecondsAboveFive);
  RUN_TEST(TestJitterAtAStandstillDoesntCount);
  RUN_TEST(TestStoppingNeedsFiveSecondsBelowThree);
  RUN_TEST(TestThresholdsFromThePhone);
  RUN_TEST(TestSamplesThatStopAreStale);
  return UNITY_END();
}
