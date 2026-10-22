#include <cstdint>
#include <string>

#include <unity.h>

#include "wifi_credentials.h"

using sarathi::core::DecodeWifiCredentials;
using sarathi::core::EncodeWifiCredentials;
using sarathi::core::kWifiBlobSize;
using sarathi::core::ValidSsid;
using sarathi::core::ValidWifiPassword;
using sarathi::core::WifiBlob;
using sarathi::core::WifiCredentials;

// Unity calls these by name, so they need external linkage and Unity's spelling.
void setUp() {}     // NOLINT(misc-use-internal-linkage,readability-identifier-naming)
void tearDown() {}  // NOLINT(misc-use-internal-linkage,readability-identifier-naming)

namespace {

void TestSsidRules() {
  TEST_ASSERT_TRUE(ValidSsid("home"));
  TEST_ASSERT_TRUE(ValidSsid(std::string(32, 'x')));
  TEST_ASSERT_TRUE(ValidSsid("\xC3\xA9t\xC3\xA9"));  // SSIDs are bytes; UTF-8 is fine
  TEST_ASSERT_FALSE(ValidSsid(""));
  TEST_ASSERT_FALSE(ValidSsid(std::string(33, 'x')));
  TEST_ASSERT_FALSE(ValidSsid(std::string("a\0b", 3)));
}

void TestPasswordRules() {
  TEST_ASSERT_TRUE(ValidWifiPassword(""));  // open network
  TEST_ASSERT_TRUE(ValidWifiPassword("12345678"));
  TEST_ASSERT_TRUE(ValidWifiPassword(std::string(63, '~')));
  TEST_ASSERT_TRUE(ValidWifiPassword(std::string(64, 'a')));   // 64 hex digits: a raw PSK
  TEST_ASSERT_FALSE(ValidWifiPassword("1234567"));             // WPA2 needs 8
  TEST_ASSERT_FALSE(ValidWifiPassword(std::string(64, 'g')));  // 64 but not hex
  TEST_ASSERT_FALSE(ValidWifiPassword(std::string(65, 'a')));
  TEST_ASSERT_FALSE(ValidWifiPassword("pass\tword"));       // not printable
  TEST_ASSERT_FALSE(ValidWifiPassword("p\xC3\xA4sswort"));  // not ASCII
}

void TestRoundTrip() {
  WifiCredentials in;
  in.ssid.Assign("Workshop 5G");
  in.password.Assign("correct horse battery");
  const WifiBlob blob = EncodeWifiCredentials(in);
  WifiCredentials out;
  TEST_ASSERT_TRUE(DecodeWifiCredentials(blob.data(), blob.size(), &out));
  TEST_ASSERT_EQUAL_STRING("Workshop 5G", out.ssid.CStr());
  TEST_ASSERT_EQUAL_STRING("correct horse battery", out.password.CStr());

  WifiCredentials open;
  open.ssid.Assign("cafe");
  const WifiBlob open_blob = EncodeWifiCredentials(open);
  TEST_ASSERT_TRUE(DecodeWifiCredentials(open_blob.data(), open_blob.size(), &out));
  TEST_ASSERT_EQUAL_STRING("", out.password.CStr());
}

void TestRejectsBadBlobs() {
  WifiCredentials in;
  in.ssid.Assign("home");
  in.password.Assign("12345678");
  WifiBlob blob = EncodeWifiCredentials(in);
  WifiCredentials out;
  out.ssid.Assign("unchanged");
  TEST_ASSERT_FALSE(DecodeWifiCredentials(nullptr, kWifiBlobSize, &out));
  TEST_ASSERT_FALSE(DecodeWifiCredentials(blob.data(), blob.size() - 1, &out));
  WifiBlob bad = blob;
  bad[0] = 2;  // unknown version
  TEST_ASSERT_FALSE(DecodeWifiCredentials(bad.data(), bad.size(), &out));
  bad = blob;
  bad[1] = 0;  // empty SSID
  TEST_ASSERT_FALSE(DecodeWifiCredentials(bad.data(), bad.size(), &out));
  bad = blob;
  bad[1] = 33;  // SSID too long
  TEST_ASSERT_FALSE(DecodeWifiCredentials(bad.data(), bad.size(), &out));
  bad = blob;
  bad[34] = 3;  // password too short for WPA2
  TEST_ASSERT_FALSE(DecodeWifiCredentials(bad.data(), bad.size(), &out));
  TEST_ASSERT_EQUAL_STRING("unchanged", out.ssid.CStr());
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(TestSsidRules);
  RUN_TEST(TestPasswordRules);
  RUN_TEST(TestRoundTrip);
  RUN_TEST(TestRejectsBadBlobs);
  return UNITY_END();
}
