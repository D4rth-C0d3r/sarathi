#include <cstdint>

#include <unity.h>

#include "app_model.h"
#include "settings_codec.h"
#include "settings_saver.h"

using sarathi::core::AppModel;
using sarathi::core::DecodeSettings;
using sarathi::core::EncodeSettings;
using sarathi::core::kSettingsBlobSize;
using sarathi::core::ScreenTimeout;
using sarathi::core::Settings;
using sarathi::core::SettingsBlob;
using sarathi::core::SettingsSaver;
using sarathi::core::ThemeId;
using sarathi::core::Units;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

Settings NonDefault() {
  Settings s;
  s.theme = ThemeId::kFighterHud;
  s.brightness_percent = 35;
  s.auto_day_night = true;
  s.units = Units::kImperial;
  s.screen_timeout = ScreenTimeout::k1min;
  s.knob_reversed = true;
  return s;
}

// ---- Codec ----

void TestRoundTrip() {
  const Settings in = NonDefault();
  const SettingsBlob blob = EncodeSettings(in);
  TEST_ASSERT_TRUE(DecodeSettings(blob.data(), blob.size()) == in);
}

void TestWrongSizeOrVersionGivesDefaults() {
  SettingsBlob blob = EncodeSettings(NonDefault());
  TEST_ASSERT_TRUE(DecodeSettings(blob.data(), kSettingsBlobSize - 1) == Settings{});
  TEST_ASSERT_TRUE(DecodeSettings(nullptr, 0) == Settings{});
  blob[0] = 99;  // unknown version
  TEST_ASSERT_TRUE(DecodeSettings(blob.data(), blob.size()) == Settings{});
}

void TestBadFieldFallsBackAlone() {
  SettingsBlob blob = EncodeSettings(NonDefault());
  blob[2] = 250;  // brightness out of range
  blob[5] = 9;    // unknown screen timeout
  const Settings s = DecodeSettings(blob.data(), blob.size());
  TEST_ASSERT_EQUAL_INT(Settings{}.brightness_percent, s.brightness_percent);
  TEST_ASSERT_EQUAL(Settings{}.screen_timeout, s.screen_timeout);
  // The other fields survive.
  TEST_ASSERT_EQUAL(ThemeId::kFighterHud, s.theme);
  TEST_ASSERT_EQUAL(Units::kImperial, s.units);
  TEST_ASSERT_TRUE(s.knob_reversed);
}

// ---- Saver ----

void TestSaverDebounces() {
  SettingsSaver saver(2000);
  saver.MarkSaved(0);
  TEST_ASSERT_FALSE(saver.ShouldSave(0, 1000));  // nothing changed
  TEST_ASSERT_FALSE(saver.ShouldSave(1, 1000));  // changed: wait
  TEST_ASSERT_FALSE(saver.ShouldSave(2, 1500));  // changed again: restart the wait
  TEST_ASSERT_FALSE(saver.ShouldSave(2, 3000));
  TEST_ASSERT_TRUE(saver.ShouldSave(2, 3500));  // 2 s after the last change: write once
  TEST_ASSERT_FALSE(saver.ShouldSave(2, 9000));
}

void TestSaverIgnoresChangeBack() {
  SettingsSaver saver(2000);
  saver.MarkSaved(5);
  TEST_ASSERT_FALSE(saver.ShouldSave(5, 100));
  TEST_ASSERT_FALSE(saver.ShouldSave(5, 5000));
}

void TestSaverRetriesAfterFailure() {
  SettingsSaver saver(2000);
  saver.MarkSaved(0);
  saver.ShouldSave(1, 0);
  TEST_ASSERT_TRUE(saver.ShouldSave(1, 2000));
  saver.SaveFailed(2000);
  TEST_ASSERT_FALSE(saver.ShouldSave(1, 3000));
  TEST_ASSERT_TRUE(saver.ShouldSave(1, 4000));
}

// ---- Model ----

void TestNewSettersAndRevision() {
  AppModel m;
  const uint32_t r0 = m.SettingsRevision();
  TEST_ASSERT_TRUE(m.SetUnits(Units::kImperial));
  TEST_ASSERT_TRUE(m.SetScreenTimeout(ScreenTimeout::k30s));
  TEST_ASSERT_TRUE(m.SetAutoDayNight(true));
  TEST_ASSERT_TRUE(m.SetKnobReversed(true));
  TEST_ASSERT_EQUAL_UINT32(r0 + 4, m.SettingsRevision());
  TEST_ASSERT_TRUE(m.SetUnits(Units::kImperial));  // no change: no new revision
  TEST_ASSERT_EQUAL_UINT32(r0 + 4, m.SettingsRevision());
  m.SetSpeed(30);  // moving: everything locked
  TEST_ASSERT_FALSE(m.SetUnits(Units::kMetric));
  TEST_ASSERT_FALSE(m.SetKnobReversed(false));
  TEST_ASSERT_EQUAL(Units::kImperial, m.State().settings.units);
}

void TestLoadSettingsBypassesLockAndRevision() {
  AppModel m;
  m.SetSpeed(30);
  const uint32_t r0 = m.SettingsRevision();
  Settings s = NonDefault();
  s.brightness_percent = 0;  // clamped
  m.LoadSettings(s);
  TEST_ASSERT_EQUAL(ThemeId::kFighterHud, m.State().settings.theme);
  TEST_ASSERT_EQUAL_INT(5, m.State().settings.brightness_percent);
  TEST_ASSERT_EQUAL_UINT32(r0, m.SettingsRevision());  // loading isn't a change to save
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestRoundTrip);
  RUN_TEST(TestWrongSizeOrVersionGivesDefaults);
  RUN_TEST(TestBadFieldFallsBackAlone);
  RUN_TEST(TestSaverDebounces);
  RUN_TEST(TestSaverIgnoresChangeBack);
  RUN_TEST(TestSaverRetriesAfterFailure);
  RUN_TEST(TestNewSettersAndRevision);
  RUN_TEST(TestLoadSettingsBypassesLockAndRevision);
  return UNITY_END();
}
