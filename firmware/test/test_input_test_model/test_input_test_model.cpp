#include <cstdint>

#include <unity.h>

#include "input_test_model.h"

using sarathi::core::ButtonGesture;
using sarathi::core::InputTestModel;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

constexpr uint32_t kDebounceMs = 50;
constexpr uint32_t kDoubleClickMs = 300;
constexpr int kStartPercent = 80;
constexpr int kStepPercent = 5;

InputTestModel MakeModel() {
  return InputTestModel(kDebounceMs, kDoubleClickMs, kStartPercent, kStepPercent);
}

void TestStartsAtInitialBacklight() {
  const InputTestModel m = MakeModel();
  TEST_ASSERT_EQUAL_INT(0, m.State().knob_count);
  TEST_ASSERT_EQUAL_INT(kStartPercent, m.State().backlight_percent);
}

void TestKnobMovesCountAndBacklight() {
  InputTestModel m = MakeModel();
  m.OnKnobStep(1);
  m.OnKnobStep(1);
  m.OnKnobStep(-1);
  TEST_ASSERT_EQUAL_INT(1, m.State().knob_count);
  TEST_ASSERT_EQUAL_INT(85, m.State().backlight_percent);
}

void TestBacklightClampsButCountKeepsGoing() {
  InputTestModel m = MakeModel();
  for (int i = 0; i < 40; ++i) {
    m.OnKnobStep(-1);
  }
  TEST_ASSERT_EQUAL_INT(-40, m.State().knob_count);
  TEST_ASSERT_EQUAL_INT(5, m.State().backlight_percent);
  for (int i = 0; i < 40; ++i) {
    m.OnKnobStep(1);
  }
  TEST_ASSERT_EQUAL_INT(100, m.State().backlight_percent);
}

void TestButtonClickReachesState() {
  InputTestModel m = MakeModel();
  uint32_t now = 1000;
  for (; now < 1100; now += 5) {
    m.OnButtonSample(true, now);
  }
  TEST_ASSERT_TRUE(m.State().button_pressed);
  for (; now < 1600; now += 5) {
    m.OnButtonSample(false, now);
  }
  TEST_ASSERT_FALSE(m.State().button_pressed);
  TEST_ASSERT_EQUAL(ButtonGesture::kClick, m.State().last_gesture);
}

void TestTouchKeepsLastPointAfterRelease() {
  InputTestModel m = MakeModel();
  m.OnTouch(true, 100, 200);
  TEST_ASSERT_TRUE(m.State().touching);
  m.OnTouch(false, 0, 0);
  TEST_ASSERT_FALSE(m.State().touching);
  TEST_ASSERT_EQUAL_INT(100, m.State().touch_x);
  TEST_ASSERT_EQUAL_INT(200, m.State().touch_y);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestStartsAtInitialBacklight);
  RUN_TEST(TestKnobMovesCountAndBacklight);
  RUN_TEST(TestBacklightClampsButCountKeepsGoing);
  RUN_TEST(TestButtonClickReachesState);
  RUN_TEST(TestTouchKeepsLastPointAfterRelease);
  return UNITY_END();
}
