#include <cstdint>

#include <unity.h>

#include "boot_health.h"
#include "ota_update.h"

using sarathi::core::BootDecision;
using sarathi::core::BootHealth;
using sarathi::core::DecideBoot;
using sarathi::core::kBootSettleMs;
using sarathi::core::Manifest;
using sarathi::core::OtaError;
using sarathi::core::OtaFailure;
using sarathi::core::OtaJob;
using sarathi::core::OtaJobType;
using sarathi::core::OtaPhase;
using sarathi::core::OtaUpdate;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

Manifest Release() {
  Manifest m;
  m.version.Assign("0.3.0");
  m.seq = 300;
  m.size = 1000;
  return m;
}

// Takes the pending job, checks its type, returns its attempt.
uint32_t ExpectJob(OtaUpdate& ota, OtaJobType type) {
  const OtaJob job = ota.TakeJob();
  TEST_ASSERT_EQUAL(type, job.type);
  return job.attempt;
}

// Drives a fresh OtaUpdate to Available and returns the attempt.
uint32_t ToAvailable(OtaUpdate& ota) {
  TEST_ASSERT_TRUE(ota.Check(true));
  const uint32_t attempt = ExpectJob(ota, OtaJobType::kConnect);
  ota.OnConnected(attempt, true);
  TEST_ASSERT_EQUAL(attempt, ExpectJob(ota, OtaJobType::kFetchManifest));
  const Manifest m = Release();
  ota.OnManifest(attempt, true, OtaError::kNone, &m);
  TEST_ASSERT_EQUAL(OtaPhase::kAvailable, ota.Phase());
  return attempt;
}

void TestHappyPath() {
  OtaUpdate ota;
  TEST_ASSERT_EQUAL(OtaPhase::kIdle, ota.Phase());
  const uint32_t attempt = ToAvailable(ota);
  TEST_ASSERT_EQUAL_STRING("0.3.0", ota.AvailableVersion().CStr());
  TEST_ASSERT_EQUAL_UINT64(1000, ota.AvailableSize());
  ExpectJob(ota, OtaJobType::kNone);  // waits for the rider

  TEST_ASSERT_TRUE(ota.Confirm());
  TEST_ASSERT_EQUAL(OtaPhase::kDownloading, ota.Phase());
  TEST_ASSERT_EQUAL(attempt, ExpectJob(ota, OtaJobType::kDownload));
  ExpectJob(ota, OtaJobType::kNone);  // handed out once

  const uint32_t before = ota.Revision();
  ota.OnProgress(attempt, 4);  // still 0 %
  TEST_ASSERT_EQUAL_UINT32(before, ota.Revision());
  ota.OnProgress(attempt, 500);
  TEST_ASSERT_EQUAL(50, ota.ProgressPercent());
  TEST_ASSERT_TRUE(ota.Revision() > before);

  ota.OnDownloaded(attempt, true);
  TEST_ASSERT_EQUAL(OtaPhase::kInstalling, ota.Phase());
  ExpectJob(ota, OtaJobType::kInstall);
  TEST_ASSERT_FALSE(ota.Cancel());  // too late to cancel
  ota.OnInstalled(attempt, OtaError::kNone, true);
  TEST_ASSERT_EQUAL(OtaPhase::kRebooting, ota.Phase());
  ExpectJob(ota, OtaJobType::kReboot);
}

void TestNoWifiSetup() {
  OtaUpdate ota;
  TEST_ASSERT_TRUE(ota.Check(false));
  TEST_ASSERT_EQUAL(OtaPhase::kFailed, ota.Phase());
  TEST_ASSERT_EQUAL(OtaFailure::kNoWifiSetup, ota.Failure());
  ExpectJob(ota, OtaJobType::kNone);  // nothing was started, so nothing to abort
}

void TestUpToDate() {
  OtaUpdate ota;
  ota.Check(true);
  const uint32_t attempt = ExpectJob(ota, OtaJobType::kConnect);
  ota.OnConnected(attempt, true);
  ExpectJob(ota, OtaJobType::kFetchManifest);
  ota.OnManifest(attempt, true, OtaError::kNotNewer, nullptr);
  TEST_ASSERT_EQUAL(OtaPhase::kUpToDate, ota.Phase());
  ExpectJob(ota, OtaJobType::kAbort);  // Wi-Fi off
  TEST_ASSERT_TRUE(ota.Check(true));   // can check again
}

void TestFailures() {
  {
    OtaUpdate ota;
    ota.Check(true);
    ota.OnConnected(ExpectJob(ota, OtaJobType::kConnect), false);
    TEST_ASSERT_EQUAL(OtaFailure::kWifiConnect, ota.Failure());
    ExpectJob(ota, OtaJobType::kAbort);
  }
  {
    OtaUpdate ota;
    ota.Check(true);
    const uint32_t attempt = ExpectJob(ota, OtaJobType::kConnect);
    ota.OnConnected(attempt, true);
    ota.OnManifest(attempt, false, OtaError::kNone, nullptr);
    TEST_ASSERT_EQUAL(OtaFailure::kNetwork, ota.Failure());
  }
  {
    OtaUpdate ota;
    ota.Check(true);
    const uint32_t attempt = ExpectJob(ota, OtaJobType::kConnect);
    ota.OnConnected(attempt, true);
    ota.OnManifest(attempt, true, OtaError::kBadSignature, nullptr);
    TEST_ASSERT_EQUAL(OtaPhase::kFailed, ota.Phase());
    TEST_ASSERT_EQUAL(OtaFailure::kManifest, ota.Failure());
    TEST_ASSERT_EQUAL(OtaError::kBadSignature, ota.Error());
  }
  {
    OtaUpdate ota;
    const uint32_t attempt = ToAvailable(ota);
    ota.Confirm();
    ota.OnDownloaded(attempt, false);
    TEST_ASSERT_EQUAL(OtaFailure::kDownload, ota.Failure());
  }
  {
    OtaUpdate ota;
    const uint32_t attempt = ToAvailable(ota);
    ota.Confirm();
    ota.OnDownloaded(attempt, true);
    ota.OnInstalled(attempt, OtaError::kHashMismatch, false);
    TEST_ASSERT_EQUAL(OtaFailure::kImage, ota.Failure());
    TEST_ASSERT_EQUAL(OtaError::kHashMismatch, ota.Error());
    ExpectJob(ota, OtaJobType::kAbort);  // discard the rejected image
  }
  {
    OtaUpdate ota;
    const uint32_t attempt = ToAvailable(ota);
    ota.Confirm();
    ota.OnDownloaded(attempt, true);
    ota.OnInstalled(attempt, OtaError::kNone, false);
    TEST_ASSERT_EQUAL(OtaFailure::kFlash, ota.Failure());
  }
}

void TestCancelIgnoresLateResults() {
  OtaUpdate ota;
  const uint32_t first = ToAvailable(ota);
  ota.Confirm();
  ExpectJob(ota, OtaJobType::kDownload);
  TEST_ASSERT_TRUE(ota.Cancel());
  TEST_ASSERT_EQUAL(OtaPhase::kIdle, ota.Phase());
  ExpectJob(ota, OtaJobType::kAbort);

  ota.OnProgress(first, 900);  // the worker hadn't noticed yet
  ota.OnDownloaded(first, true);
  TEST_ASSERT_EQUAL(OtaPhase::kIdle, ota.Phase());

  // A new attempt ignores results from the old one.
  TEST_ASSERT_TRUE(ota.Check(true));
  const uint32_t second = ExpectJob(ota, OtaJobType::kConnect);
  TEST_ASSERT_NOT_EQUAL(first, second);
  ota.OnConnected(first, true);
  TEST_ASSERT_EQUAL(OtaPhase::kConnecting, ota.Phase());
  ota.OnConnected(second, true);
  TEST_ASSERT_EQUAL(OtaPhase::kChecking, ota.Phase());
}

void TestCommandsOnlyWhereTheyApply() {
  OtaUpdate ota;
  TEST_ASSERT_FALSE(ota.Confirm());
  TEST_ASSERT_FALSE(ota.Cancel());
  TEST_ASSERT_TRUE(ota.Check(true));
  TEST_ASSERT_FALSE(ota.Check(true));  // already busy
  TEST_ASSERT_FALSE(ota.Confirm());    // nothing offered yet
  TEST_ASSERT_TRUE(ota.Cancel());
  TEST_ASSERT_EQUAL(0, ota.ProgressPercent());
}

void TestBootHealth() {
  BootHealth h;
  TEST_ASSERT_EQUAL(BootDecision::kNotPending, DecideBoot(h));
  h.pending_verify = true;
  h.uptime_ms = kBootSettleMs;
  TEST_ASSERT_EQUAL(BootDecision::kWait, DecideBoot(h));  // UI not up
  h.ui_ready = true;
  h.uptime_ms = kBootSettleMs - 1;
  TEST_ASSERT_EQUAL(BootDecision::kWait, DecideBoot(h));  // not settled
  h.uptime_ms = kBootSettleMs;
  TEST_ASSERT_EQUAL(BootDecision::kConfirm, DecideBoot(h));  // BLE not required yet
  TEST_ASSERT_EQUAL(BootDecision::kWait, DecideBoot(h, true));
  h.ble_ready = true;
  TEST_ASSERT_EQUAL(BootDecision::kConfirm, DecideBoot(h, true));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestHappyPath);
  RUN_TEST(TestNoWifiSetup);
  RUN_TEST(TestUpToDate);
  RUN_TEST(TestFailures);
  RUN_TEST(TestCancelIgnoresLateResults);
  RUN_TEST(TestCommandsOnlyWhereTheyApply);
  RUN_TEST(TestBootHealth);
  return UNITY_END();
}
