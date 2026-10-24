#include <unity.h>

#include "radio_policy.h"

using sarathi::core::JobStartsWifi;
using sarathi::core::kBleResumeDelayMs;
using sarathi::core::OtaJobType;
using sarathi::core::OtaPhase;
using sarathi::core::RadioPolicy;
using sarathi::core::WifiInUse;
using sarathi::core::WifiSetupJob;
using sarathi::core::WifiSetupPhase;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

void TestWifiInUseDuringUpdates() {
  TEST_ASSERT_FALSE(WifiInUse(OtaPhase::kIdle, WifiSetupPhase::kIdle));
  TEST_ASSERT_TRUE(WifiInUse(OtaPhase::kConnecting, WifiSetupPhase::kIdle));
  TEST_ASSERT_TRUE(WifiInUse(OtaPhase::kAvailable, WifiSetupPhase::kIdle));
  TEST_ASSERT_TRUE(WifiInUse(OtaPhase::kDownloading, WifiSetupPhase::kIdle));
  TEST_ASSERT_TRUE(WifiInUse(OtaPhase::kRebooting, WifiSetupPhase::kIdle));
  TEST_ASSERT_FALSE(WifiInUse(OtaPhase::kUpToDate, WifiSetupPhase::kIdle));
  TEST_ASSERT_FALSE(WifiInUse(OtaPhase::kFailed, WifiSetupPhase::kIdle));
}

void TestWifiInUseDuringSetup() {
  TEST_ASSERT_TRUE(WifiInUse(OtaPhase::kIdle, WifiSetupPhase::kStarting));
  TEST_ASSERT_TRUE(WifiInUse(OtaPhase::kIdle, WifiSetupPhase::kWaitingForPhone));
  TEST_ASSERT_TRUE(WifiInUse(OtaPhase::kIdle, WifiSetupPhase::kJoined));
  TEST_ASSERT_FALSE(WifiInUse(OtaPhase::kIdle, WifiSetupPhase::kFailed));
}

void TestJobsThatStartWifi() {
  TEST_ASSERT_TRUE(JobStartsWifi(OtaJobType::kConnect));
  TEST_ASSERT_FALSE(JobStartsWifi(OtaJobType::kFetchManifest));
  TEST_ASSERT_FALSE(JobStartsWifi(OtaJobType::kAbort));
  TEST_ASSERT_TRUE(JobStartsWifi(WifiSetupJob::kStartAccessPoint));
  TEST_ASSERT_FALSE(JobStartsWifi(WifiSetupJob::kStopAccessPoint));
}

void TestBlePausesAtOnceAndResumesLater() {
  RadioPolicy policy;
  TEST_ASSERT_FALSE(policy.Update(false, 1000));
  TEST_ASSERT_TRUE(policy.Update(true, 2000));
  TEST_ASSERT_TRUE(policy.Update(true, 9000));
  TEST_ASSERT_TRUE(policy.Update(false, 9001));                          // Wi-Fi just went off
  TEST_ASSERT_TRUE(policy.Update(false, 9000 + kBleResumeDelayMs - 1));  // still settling
  TEST_ASSERT_FALSE(policy.Update(false, 9000 + kBleResumeDelayMs));
  TEST_ASSERT_FALSE(policy.BlePaused());
}

void TestWifiAgainRestartsTheDelay() {
  RadioPolicy policy;
  policy.Update(true, 0);
  policy.Update(false, 100);
  policy.Update(true, 2000);
  TEST_ASSERT_TRUE(policy.Update(false, 2000 + kBleResumeDelayMs - 1));
  TEST_ASSERT_FALSE(policy.Update(false, 2000 + kBleResumeDelayMs));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestWifiInUseDuringUpdates);
  RUN_TEST(TestWifiInUseDuringSetup);
  RUN_TEST(TestJobsThatStartWifi);
  RUN_TEST(TestBlePausesAtOnceAndResumesLater);
  RUN_TEST(TestWifiAgainRestartsTheDelay);
  return UNITY_END();
}
