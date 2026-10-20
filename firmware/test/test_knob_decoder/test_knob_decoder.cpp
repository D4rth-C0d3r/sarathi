#include <unity.h>

#include "knob_decoder.h"

using sarathi::core::KnobDecoder;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

constexpr uint8_t kPerDetent = 4;

// One full quadrature cycle in the "+" direction (A leads B): AB 00 -> 10 -> 11 -> 01 -> 00.
// Which physical direction this is gets confirmed on the device.
int Forward(KnobDecoder& d) {
  int sum = 0;
  sum += d.Update(true, false);
  sum += d.Update(true, true);
  sum += d.Update(false, true);
  sum += d.Update(false, false);
  return sum;
}

// B leads A: AB 00 -> 01 -> 11 -> 10 -> 00.
int Backward(KnobDecoder& d) {
  int sum = 0;
  sum += d.Update(false, true);
  sum += d.Update(true, true);
  sum += d.Update(true, false);
  sum += d.Update(false, false);
  return sum;
}

void TestFullCycleForwardIsOneStep() {
  KnobDecoder d(kPerDetent);
  d.Reset(false, false);
  TEST_ASSERT_EQUAL_INT(1, Forward(d));
}

void TestFullCycleBackwardIsMinusOneStep() {
  KnobDecoder d(kPerDetent);
  d.Reset(false, false);
  TEST_ASSERT_EQUAL_INT(-1, Backward(d));
}

void TestStepFiresOnlyOnFourthTransition() {
  KnobDecoder d(kPerDetent);
  d.Reset(false, false);
  TEST_ASSERT_EQUAL_INT(0, d.Update(true, false));
  TEST_ASSERT_EQUAL_INT(0, d.Update(true, true));
  TEST_ASSERT_EQUAL_INT(0, d.Update(false, true));
  TEST_ASSERT_EQUAL_INT(1, d.Update(false, false));
}

void TestRepeatedSamplesDoNotCount() {
  KnobDecoder d(kPerDetent);
  d.Reset(false, false);
  for (int i = 0; i < 10; ++i) {
    TEST_ASSERT_EQUAL_INT(0, d.Update(false, false));
  }
}

void TestContactBounceCancelsOut() {
  KnobDecoder d(kPerDetent);
  d.Reset(false, false);
  // Bounce on A: 00 -> 10 -> 00 -> 10, then finish the cycle.
  int sum = d.Update(true, false) + d.Update(false, false) + d.Update(true, false);
  sum += d.Update(true, true) + d.Update(false, true) + d.Update(false, false);
  TEST_ASSERT_EQUAL_INT(1, sum);
}

void TestSkippedTransitionIsIgnored() {
  KnobDecoder d(kPerDetent);
  d.Reset(false, false);
  TEST_ASSERT_EQUAL_INT(0, d.Update(true, true));  // 00 -> 11 is invalid
  TEST_ASSERT_EQUAL_INT(0, d.Update(false, false));
}

void TestManyDetents() {
  KnobDecoder d(kPerDetent);
  d.Reset(false, false);
  int total = 0;
  for (int i = 0; i < 5; ++i) {
    total += Forward(d);
  }
  for (int i = 0; i < 2; ++i) {
    total += Backward(d);
  }
  TEST_ASSERT_EQUAL_INT(3, total);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestFullCycleForwardIsOneStep);
  RUN_TEST(TestFullCycleBackwardIsMinusOneStep);
  RUN_TEST(TestStepFiresOnlyOnFourthTransition);
  RUN_TEST(TestRepeatedSamplesDoNotCount);
  RUN_TEST(TestContactBounceCancelsOut);
  RUN_TEST(TestSkippedTransitionIsIgnored);
  RUN_TEST(TestManyDetents);
  return UNITY_END();
}
