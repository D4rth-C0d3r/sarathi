// On-device OTA tests (ADR-0004), run with `pio test -e crowpanel21-test`:
//   - every golden vector with real ES256 (mbedTLS) and real SHA-256 on the ESP32-S3
//   - the production key table parses
//   - with Wi-Fi credentials in the build environment: Wi-Fi, HTTPS against the root CA
//     bundle across a cross-host redirect, and verification of the fetched manifest
// The test firmware replaces the app; flash the app again afterwards.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <unity.h>

#include "golden_vectors.h"
#include "https_get.h"
#include "mbedtls_verifier.h"
#include "ota_manifest.h"
#include "ota_trusted_keys.h"
#include "sha256_stream.h"
#include "wifi_link.h"

#ifndef SARATHI_TEST_WIFI_SSID
#define SARATHI_TEST_WIFI_SSID ""
#endif
#ifndef SARATHI_TEST_WIFI_PASSWORD
#define SARATHI_TEST_WIFI_PASSWORD ""
#endif

namespace core = sarathi::core;
namespace ota = sarathi::ota;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

// A vector already on GitHub at a fixed commit, so the network test never depends on
// unpushed or regenerated files. Its own throwaway test key signed it.
constexpr const char* kPinnedUrl =
    "https://github.com/D4rth-C0d3r/sarathi/raw/1d1ac7d/docs/ota/vectors/valid.jws";
constexpr core::TrustedKey kPinnedKey = {
    "test-only",
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAERBYb0imwDpjyIHOQxPyE1HDwbKHf\n"
    "FMHrQYBtVXuSfZqQStynxBebf/WsQ/SfTZNiwB+rP/1zS7e1hmaKuUJ1UQ==\n"
    "-----END PUBLIC KEY-----\n"};

core::ManifestScratch scratch;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Measurements go through Unity, which owns the serial port in test builds.
void Log(const char* format, ...) {
  char line[160];
  va_list args;
  va_start(args, format);
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  TEST_MESSAGE(line);
}

core::ManifestContext GoldenContext() {
  return {golden::kBoard, golden::kRunningSeq, golden::kSlotSize};
}

void TestGoldenVectorsWithRealCrypto() {
  const core::TrustedKey key{golden::kKid, golden::kPublicKeyPem};
  ota::MbedtlsVerifier verifier;
  TEST_ASSERT_EQUAL(1, verifier.Begin(&key, 1));
  const int64_t start = esp_timer_get_time();
  int verified = 0;
  for (const golden::Case& c : golden::kCases) {
    core::Manifest manifest;
    core::OtaError error =
        core::VerifyManifest(c.manifest, GoldenContext(), verifier, scratch, &manifest);
    verified += c.signature_valid >= 0 ? 1 : 0;
    if (c.image != nullptr && error == core::OtaError::kNone) {
      ota::Sha256Stream sha;
      sha.Update(c.image, c.image_size);
      core::Sha256 digest{};
      TEST_ASSERT_TRUE(sha.Finish(&digest));
      core::ImageCheck check(manifest.size, manifest.sha256);
      static_cast<void>(check.Add(c.image_size));
      error = check.Finish(digest);
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expect, core::OtaErrorCode(error), c.name);
  }
  const auto ms = static_cast<double>(esp_timer_get_time() - start) / 1000.0;
  Log("%u vectors (%d signature checks) in %.1f ms",
      static_cast<unsigned>(sizeof(golden::kCases) / sizeof(golden::kCases[0])), verified, ms);
}

void TestProductionKeysParse() {
  ota::MbedtlsVerifier verifier;
  TEST_ASSERT_EQUAL(core::kTrustedOtaKeys.size(),
                    verifier.Begin(core::kTrustedOtaKeys.data(), core::kTrustedOtaKeys.size()));
  TEST_ASSERT_TRUE(verifier.Trusts("sarathi-ota-1"));
  TEST_ASSERT_FALSE(verifier.Trusts("test-only"));
}

void TestRefusesPlainHttp() {
  ota::FetchReport report;
  TEST_ASSERT_EQUAL(ota::FetchResult::kInsecureRedirect,
                    ota::HttpsGet("http://github.com/", 100, nullptr, nullptr, &report));
}

struct Body {
  char data[core::kMaxManifestBytes + 3] = {};
  size_t size = 0;
};

bool Collect(void* context, const uint8_t* data, size_t size) {
  auto* body = static_cast<Body*>(context);
  if (body->size + size >= sizeof(body->data)) {
    return false;
  }
  std::memcpy(&body->data[body->size], data, size);
  body->size += size;
  return true;
}

void LogHeap(const char* when) {
  Log("%s: internal heap %u KB free (min %u KB, largest block %u KB)", when,
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
      static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
}

void TestNetworkFetchAndVerify() {
  if (std::strlen(SARATHI_TEST_WIFI_SSID) == 0) {
    TEST_IGNORE_MESSAGE("set SARATHI_WIFI_SSID and SARATHI_WIFI_PASSWORD to run");
  }
  LogHeap("before Wi-Fi");
  core::WifiCredentials credentials;
  credentials.ssid.Assign(SARATHI_TEST_WIFI_SSID);
  credentials.password.Assign(SARATHI_TEST_WIFI_PASSWORD);
  TEST_ASSERT_TRUE_MESSAGE(ota::WifiConnect(credentials), "Wi-Fi did not connect");
  LogHeap("Wi-Fi up");

  static Body body;
  ota::FetchReport report;
  const int64_t start = esp_timer_get_time();
  const ota::FetchResult result =
      ota::HttpsGet(kPinnedUrl, sizeof(body.data) - 1, Collect, &body, &report);
  Log("fetch: %s, HTTP %d, %d redirect(s), %u B in %.0f ms", ota::FetchResultName(result),
      report.status, report.redirects, static_cast<unsigned>(body.size),
      static_cast<double>(esp_timer_get_time() - start) / 1000.0);
  LogHeap("after HTTPS");
  TEST_ASSERT_EQUAL_STRING("ok", ota::FetchResultName(result));
  TEST_ASSERT_EQUAL(200, report.status);
  TEST_ASSERT_TRUE(report.redirects >= 1);  // github.com -> raw.githubusercontent.com

  ota::MbedtlsVerifier verifier;
  TEST_ASSERT_EQUAL(1, verifier.Begin(&kPinnedKey, 1));
  core::Manifest manifest;
  TEST_ASSERT_EQUAL_STRING(
      "ok", core::OtaErrorCode(core::VerifyManifest({body.data, body.size}, GoldenContext(),
                                                    verifier, scratch, &manifest)));
  TEST_ASSERT_EQUAL_STRING("0.3.0", manifest.version.CStr());

  // The production "latest" URL: there's no release with a manifest yet, so expect GitHub's
  // 404, reached over TLS through its redirect.
  body.size = 0;
  const ota::FetchResult latest =
      ota::HttpsGet("https://github.com/D4rth-C0d3r/sarathi/releases/latest/download/manifest.jws",
                    sizeof(body.data) - 1, Collect, &body, &report);
  Log("latest manifest: %s, HTTP %d", ota::FetchResultName(latest), report.status);
  TEST_ASSERT_TRUE(latest == ota::FetchResult::kOk || latest == ota::FetchResult::kHttpStatus);
  ota::WifiOff();
  LogHeap("Wi-Fi off");
}

}  // namespace

void setup() {
  delay(2000);  // let the USB serial port come up so the runner sees every line
  UNITY_BEGIN();
  RUN_TEST(TestGoldenVectorsWithRealCrypto);
  RUN_TEST(TestProductionKeysParse);
  RUN_TEST(TestRefusesPlainHttp);
  RUN_TEST(TestNetworkFetchAndVerify);
  UNITY_END();
}

void loop() {}
