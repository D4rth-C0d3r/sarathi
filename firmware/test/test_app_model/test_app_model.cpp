#include <unity.h>

#include "app_model.h"

using sarathi::core::AppModel;
using sarathi::core::CallPhase;
using sarathi::core::Command;
using sarathi::core::CommandType;
using sarathi::core::kCommandQueueLength;
using sarathi::core::Maneuver;
using sarathi::core::ManeuverType;
using sarathi::core::MediaState;
using sarathi::core::ThemeId;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

void TestGuidanceActivatesNav() {
  AppModel m;
  TEST_ASSERT_FALSE(m.State().nav.active);
  m.SetGuidance(Maneuver{ManeuverType::kRight, 0}, 350, "Station Road", 240);
  TEST_ASSERT_TRUE(m.State().nav.active);
  TEST_ASSERT_EQUAL_UINT32(350, m.State().nav.distance_m);
  TEST_ASSERT_EQUAL_STRING("Station Road", m.State().nav.street.CStr());
  m.EndNav();
  TEST_ASSERT_FALSE(m.State().nav.active);
  TEST_ASSERT_FALSE(m.State().nav.has_window);
}

void TestSettingsLockedWhileMoving() {
  AppModel m;
  m.SetSpeed(4);
  TEST_ASSERT_FALSE(m.SettingsLocked());
  TEST_ASSERT_TRUE(m.SetBrightness(50));
  m.SetSpeed(5);
  TEST_ASSERT_TRUE(m.SettingsLocked());
  TEST_ASSERT_FALSE(m.SetBrightness(90));
  TEST_ASSERT_FALSE(m.SetTheme(ThemeId::kRacing));
  TEST_ASSERT_EQUAL_INT(50, m.State().settings.brightness_percent);
  TEST_ASSERT_EQUAL(ThemeId::kClassic, m.State().settings.theme);
}

void TestBrightnessIsClamped() {
  AppModel m;
  TEST_ASSERT_TRUE(m.SetBrightness(0));
  TEST_ASSERT_EQUAL_INT(5, m.State().settings.brightness_percent);
  TEST_ASSERT_TRUE(m.SetBrightness(200));
  TEST_ASSERT_EQUAL_INT(100, m.State().settings.brightness_percent);
}

void TestCallActionsOnlyWhenTheyApply() {
  AppModel m;
  Command c;
  TEST_ASSERT_FALSE(m.AcceptCall());
  TEST_ASSERT_FALSE(m.RejectCall());
  m.SetCall(CallPhase::kRinging, "Demo Caller", 1000);
  TEST_ASSERT_TRUE(m.AcceptCall());
  TEST_ASSERT_TRUE(m.TakeCommand(c));
  TEST_ASSERT_EQUAL(CommandType::kCallAccept, c.type);
  // The model doesn't change the phase itself; the phone (data source) does.
  TEST_ASSERT_EQUAL(CallPhase::kRinging, m.State().call.phase);
  m.SetCall(CallPhase::kActive, "Demo Caller", 2000);
  TEST_ASSERT_FALSE(m.AcceptCall());
  TEST_ASSERT_TRUE(m.RejectCall());  // hang up
  TEST_ASSERT_TRUE(m.TakeCommand(c));
  TEST_ASSERT_EQUAL(CommandType::kCallReject, c.type);
}

void TestCallPhaseTimestamp() {
  AppModel m;
  m.SetCall(CallPhase::kRinging, "Demo Caller", 1000);
  m.SetCall(CallPhase::kRinging, "Demo Caller", 1500);  // same phase: keeps the start time
  TEST_ASSERT_EQUAL_UINT32(1000, m.State().call.phase_since_ms);
  m.SetCall(CallPhase::kIdle, "Demo Caller", 3000);
  TEST_ASSERT_EQUAL_UINT32(3000, m.State().call.phase_since_ms);
  TEST_ASSERT_TRUE(m.State().call.caller.Empty());
}

void TestMediaCommandsNeedMedia() {
  AppModel m;
  TEST_ASSERT_FALSE(m.MediaPlayPause());
  MediaState media{};
  media.available = true;
  m.SetMedia(media);
  TEST_ASSERT_TRUE(m.MediaNext());
  TEST_ASSERT_TRUE(m.MediaPlayPause());
  Command c;
  TEST_ASSERT_TRUE(m.TakeCommand(c));
  TEST_ASSERT_EQUAL(CommandType::kMediaNext, c.type);  // oldest first
  TEST_ASSERT_TRUE(m.TakeCommand(c));
  TEST_ASSERT_EQUAL(CommandType::kMediaPlayPause, c.type);
  TEST_ASSERT_FALSE(m.TakeCommand(c));
}

void TestQueueDropsWhenFull() {
  AppModel m;
  MediaState media{};
  media.available = true;
  m.SetMedia(media);
  for (size_t i = 0; i < kCommandQueueLength; ++i) {
    TEST_ASSERT_TRUE(m.MediaNext());
  }
  TEST_ASSERT_FALSE(m.MediaPrev());
  Command c;
  size_t taken = 0;
  while (m.TakeCommand(c)) {
    TEST_ASSERT_EQUAL(CommandType::kMediaNext, c.type);
    ++taken;
  }
  TEST_ASSERT_EQUAL_UINT(kCommandQueueLength, taken);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestGuidanceActivatesNav);
  RUN_TEST(TestSettingsLockedWhileMoving);
  RUN_TEST(TestBrightnessIsClamped);
  RUN_TEST(TestCallActionsOnlyWhenTheyApply);
  RUN_TEST(TestCallPhaseTimestamp);
  RUN_TEST(TestMediaCommandsNeedMedia);
  RUN_TEST(TestQueueDropsWhenFull);
  return UNITY_END();
}
