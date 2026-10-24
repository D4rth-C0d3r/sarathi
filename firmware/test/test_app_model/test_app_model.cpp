#include <unity.h>

#include "app_model.h"

using sarathi::core::AppModel;
using sarathi::core::CallPhase;
using sarathi::core::Capabilities;
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

Capabilities NoCallControlNoSpeed() {
  Capabilities caps;
  caps.call_accept = false;
  caps.call_reject = false;
  caps.speed = false;
  return caps;
}

void TestCallActionsFollowCapabilities() {
  AppModel m;
  m.SetCapabilities(NoCallControlNoSpeed());
  m.SetCall(CallPhase::kRinging, "Contact A", 0);
  TEST_ASSERT_FALSE(m.AcceptCall());
  TEST_ASSERT_FALSE(m.RejectCall());
  TEST_ASSERT_TRUE(m.DismissCall());
  Command c;
  TEST_ASSERT_TRUE(m.TakeCommand(c));
  TEST_ASSERT_EQUAL(CommandType::kCallDismiss, c.type);
  TEST_ASSERT_FALSE(m.TakeCommand(c));
}

void TestDismissOnlyWithoutReject() {
  AppModel m;  // default capabilities: reject is supported, so dismiss isn't offered
  m.SetCall(CallPhase::kRinging, "Contact A", 0);
  TEST_ASSERT_FALSE(m.DismissCall());
  m.SetCapabilities(NoCallControlNoSpeed());
  m.SetCall(CallPhase::kIdle, "", 0);
  TEST_ASSERT_FALSE(m.DismissCall());
}

void TestMediaCommandsFollowCapabilities() {
  AppModel m;
  MediaState media{};
  media.available = true;
  m.SetMedia(media);
  Capabilities caps;
  caps.media_control = false;
  m.SetCapabilities(caps);
  TEST_ASSERT_FALSE(m.MediaPlayPause());
  TEST_ASSERT_FALSE(m.MediaNext());
  TEST_ASSERT_FALSE(m.MediaPrev());
}

void TestWithoutSpeedSettingsLockWhileNavigating() {
  AppModel m;
  m.SetCapabilities(NoCallControlNoSpeed());
  TEST_ASSERT_FALSE(m.SettingsLocked());
  m.SetPhoneGuidance("200 m", "Turn left onto Station Road", "12 min");
  TEST_ASSERT_TRUE(m.SettingsLocked());
  TEST_ASSERT_FALSE(m.SetTheme(ThemeId::kRacing));
  m.EndNav();
  TEST_ASSERT_FALSE(m.SettingsLocked());
  TEST_ASSERT_TRUE(m.SetTheme(ThemeId::kRacing));
}

void TestWithSpeedNavigatingDoesNotLock() {
  AppModel m;
  m.SetGuidance(Maneuver{ManeuverType::kLeft, 0}, 200, "Station Road", 60);
  m.SetSpeed(0);
  TEST_ASSERT_FALSE(m.SettingsLocked());
}

void TestPhoneGuidanceReplacesNumbers() {
  AppModel m;
  m.SetGuidance(Maneuver{ManeuverType::kLeft, 0}, 200, "Station Road", 60);
  m.SetPhoneGuidance("0.3 mi", "Keep right", "1 h 5 min");
  const auto& nav = m.State().nav;
  TEST_ASSERT_TRUE(nav.active);
  TEST_ASSERT_EQUAL(ManeuverType::kNone, nav.next.type);
  TEST_ASSERT_EQUAL_UINT32(0, nav.distance_m);
  TEST_ASSERT_EQUAL_STRING("0.3 mi", nav.distance_text.CStr());
  TEST_ASSERT_EQUAL_STRING("Keep right", nav.street.CStr());
  TEST_ASSERT_EQUAL_STRING("1 h 5 min", nav.remaining_text.CStr());
}

void TestPhoneBatteryIsClamped() {
  AppModel m;
  TEST_ASSERT_FALSE(m.State().phone.battery_valid);
  m.SetPhoneBattery(150, true);
  TEST_ASSERT_TRUE(m.State().phone.battery_valid);
  TEST_ASSERT_EQUAL_UINT8(100, m.State().phone.battery_percent);
  TEST_ASSERT_TRUE(m.State().phone.charging);
}

void TestPairingNeedsARequest() {
  AppModel m;
  TEST_ASSERT_FALSE(m.AcceptPairing());
  TEST_ASSERT_FALSE(m.RejectPairing());
  m.SetPairingRequest(708361, 500);
  TEST_ASSERT_TRUE(m.State().pairing.requested);
  TEST_ASSERT_EQUAL_UINT32(708361, m.State().pairing.code);
  TEST_ASSERT_TRUE(m.AcceptPairing());
  TEST_ASSERT_FALSE(m.State().pairing.requested);  // answered: the prompt closes
  TEST_ASSERT_FALSE(m.AcceptPairing());
  Command c;
  TEST_ASSERT_TRUE(m.TakeCommand(c));
  TEST_ASSERT_EQUAL(CommandType::kPairAccept, c.type);
  m.SetPairingRequest(123456, 900);
  TEST_ASSERT_TRUE(m.RejectPairing());
  TEST_ASSERT_TRUE(m.TakeCommand(c));
  TEST_ASSERT_EQUAL(CommandType::kPairReject, c.type);
}

void TestForgetPhonesFollowsCapabilityAndLock() {
  AppModel m;
  TEST_ASSERT_FALSE(m.ForgetPhones());  // no BLE pairing (the demo)
  Capabilities caps = NoCallControlNoSpeed();
  caps.phone_pairing = true;
  m.SetCapabilities(caps);
  m.SetPhoneGuidance("200 m", "Turn left", "5 min");  // navigating, no speed: locked
  TEST_ASSERT_FALSE(m.ForgetPhones());
  m.EndNav();
  TEST_ASSERT_TRUE(m.ForgetPhones());
  Command c;
  TEST_ASSERT_TRUE(m.TakeCommand(c));
  TEST_ASSERT_EQUAL(CommandType::kForgetPhones, c.type);
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
  RUN_TEST(TestCallActionsFollowCapabilities);
  RUN_TEST(TestDismissOnlyWithoutReject);
  RUN_TEST(TestMediaCommandsFollowCapabilities);
  RUN_TEST(TestWithoutSpeedSettingsLockWhileNavigating);
  RUN_TEST(TestWithSpeedNavigatingDoesNotLock);
  RUN_TEST(TestPhoneGuidanceReplacesNumbers);
  RUN_TEST(TestPhoneBatteryIsClamped);
  RUN_TEST(TestPairingNeedsARequest);
  RUN_TEST(TestForgetPhonesFollowsCapabilityAndLock);
  return UNITY_END();
}
