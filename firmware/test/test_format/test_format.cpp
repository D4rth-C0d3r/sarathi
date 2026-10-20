#include <array>

#include <unity.h>

#include "format.h"

using sarathi::core::FormatClock;
using sarathi::core::FormatDistance;
using sarathi::core::FormatDuration;
using sarathi::core::FormatManeuver;
using sarathi::core::Maneuver;
using sarathi::core::ManeuverType;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

std::array<char, 24> buf{};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

const char* Distance(uint32_t m) {
  FormatDistance(m, buf.data(), buf.size());
  return buf.data();
}

const char* Duration(uint32_t s) {
  FormatDuration(s, buf.data(), buf.size());
  return buf.data();
}

const char* Clock(uint32_t s) {
  FormatClock(s, buf.data(), buf.size());
  return buf.data();
}

const char* ManeuverText(ManeuverType type, uint8_t exit = 0) {
  FormatManeuver(Maneuver{type, exit}, buf.data(), buf.size());
  return buf.data();
}

void TestDistanceMetres() {
  TEST_ASSERT_EQUAL_STRING("0 m", Distance(0));
  TEST_ASSERT_EQUAL_STRING("350 m", Distance(347));
  TEST_ASSERT_EQUAL_STRING("990 m", Distance(994));
}

void TestDistanceKilometres() {
  TEST_ASSERT_EQUAL_STRING("1.0 km", Distance(995));
  TEST_ASSERT_EQUAL_STRING("1.2 km", Distance(1234));
  TEST_ASSERT_EQUAL_STRING("9.9 km", Distance(9949));
  TEST_ASSERT_EQUAL_STRING("10 km", Distance(9950));
  TEST_ASSERT_EQUAL_STRING("123 km", Distance(123456));
}

const char* Imperial(uint32_t m) {
  FormatDistance(m, sarathi::core::Units::kImperial, buf.data(), buf.size());
  return buf.data();
}

void TestDistanceImperial() {
  TEST_ASSERT_EQUAL_STRING("0 ft", Imperial(0));
  TEST_ASSERT_EQUAL_STRING("330 ft", Imperial(100));  // 328 ft, rounded to 10
  TEST_ASSERT_EQUAL_STRING("520 ft", Imperial(160));  // just under 0.1 mi
  TEST_ASSERT_EQUAL_STRING("0.1 mi", Imperial(161));
  TEST_ASSERT_EQUAL_STRING("1.0 mi", Imperial(1609));
  TEST_ASSERT_EQUAL_STRING("10 mi", Imperial(16093));
  TEST_ASSERT_EQUAL_STRING("77 mi", Imperial(123456));
  FormatDistance(1234, sarathi::core::Units::kMetric, buf.data(), buf.size());
  TEST_ASSERT_EQUAL_STRING("1.2 km", buf.data());
}

void TestDuration() {
  TEST_ASSERT_EQUAL_STRING("< 1 min", Duration(59));
  TEST_ASSERT_EQUAL_STRING("1 min", Duration(60));
  TEST_ASSERT_EQUAL_STRING("4 min", Duration(250));
  TEST_ASSERT_EQUAL_STRING("1 h 05", Duration(3900));
}

void TestClock() {
  TEST_ASSERT_EQUAL_STRING("0:00", Clock(0));
  TEST_ASSERT_EQUAL_STRING("1:35", Clock(95));
  TEST_ASSERT_EQUAL_STRING("1:00:01", Clock(3601));
}

void TestManeuvers() {
  TEST_ASSERT_EQUAL_STRING("Right", ManeuverText(ManeuverType::kRight));
  TEST_ASSERT_EQUAL_STRING("Slight left", ManeuverText(ManeuverType::kSlightLeft));
  TEST_ASSERT_EQUAL_STRING("Exit 2", ManeuverText(ManeuverType::kRoundabout, 2));
  TEST_ASSERT_EQUAL_STRING("Roundabout", ManeuverText(ManeuverType::kRoundabout));
  TEST_ASSERT_EQUAL_STRING("Arrive", ManeuverText(ManeuverType::kArrive));
  TEST_ASSERT_EQUAL_STRING("", ManeuverText(ManeuverType::kNone));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestDistanceMetres);
  RUN_TEST(TestDistanceKilometres);
  RUN_TEST(TestDistanceImperial);
  RUN_TEST(TestDuration);
  RUN_TEST(TestClock);
  RUN_TEST(TestManeuvers);
  return UNITY_END();
}
